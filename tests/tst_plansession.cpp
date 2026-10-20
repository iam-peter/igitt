#include "core/DiffParser.h"
#include "core/PatchBuilder.h"
#include "core/PlanSession.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestPlanSession : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Test");
        qputenv("GIT_AUTHOR_EMAIL", "test@example.com");
        qputenv("GIT_COMMITTER_NAME", "Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.com");
    }

    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_git.setWorkDir(m_dir->path());
        git({"init", "-q", "-b", "main"});
        write("f.txt", lines({}));
        git({"add", "f.txt"});
        git({"commit", "-q", "-m", "A"});
        write("f.txt", lines({{9, "nine (B)"}}));
        git({"commit", "-q", "-a", "-m", "B"});
    }

    void plansWithoutTouchingTheRepository()
    {
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        const QByteArray realState = repoState();

        PlanSession plan;
        QString error;
        QVERIFY2(plan.begin(m_git, error), qPrintable(error));
        QVERIFY2(plan.add(entry(PlanEntry::StagePaths, {u"f.txt"_s}), error), qPrintable(error));
        PlanEntry commit = entry(PlanEntry::Commit);
        commit.message = u"C\n\nBody"_s;
        QVERIFY2(plan.add(commit, error), qPrintable(error));

        QCOMPARE(repoState(), realState);
        const StatusInfo simulated = loadSimulatedStatus(plan.source());
        QCOMPARE(simulated.headSha, plan.head());
        QVERIFY(simulated.entries.isEmpty()); // everything committed, in the plan
        QCOMPARE(git({"log", "-1", "--format=%B", plan.head()}), "C\n\nBody\n\n"_ba);
        QCOMPARE(git({"rev-parse", plan.head() + u"~1"_s}).trimmed(), rev("HEAD").toLatin1());

        QVERIFY2(plan.execute(error), qPrintable(error));
        QCOMPARE(rev("HEAD"), plan.head());
        QVERIFY(git({"status", "--porcelain"}).isEmpty());
        QVERIFY(git({"reflog", "-1"}).contains("igitt: execute plan"));
    }

    void seesEditsMadeInTheSameSecondAsTheIndex()
    {
        // A same-size edit carrying exactly the timestamp git cached for the
        // file: only git's racy-index check, which compares with the index
        // file's own modification time, notices it. Copies of the index must
        // keep that time.
        git({"config", "core.trustctime", "false"});
        // Let a second pass, then have git cache real stat data for the file
        // (entries written in the file's own second carry none).
        QTest::qWait(1100);
        git({"update-index", "--refresh"});
        const QRegularExpression mtime(u"mtime: (\\d+):(\\d+)"_s);
        const QRegularExpressionMatch cached = mtime.match(QString::fromUtf8(git({"ls-files", "--debug", "f.txt"})));
        QVERIFY(cached.hasMatch());
        const QString stamp = u"@%1.%2"_s.arg(cached.captured(1), cached.captured(2).rightJustified(9, u'0'));
        write("f.txt", lines({{2, "Z"}, {9, "nine (B)"}})); // "2" -> "Z": same size
        QCOMPARE(QProcess::execute(u"touch"_s, {u"-m"_s, u"-d"_s, stamp, m_dir->filePath("f.txt"),
                                                 m_dir->filePath(".git/index")}), 0);
        QVERIFY(!git({"status", "--porcelain"}).isEmpty()); // git itself sees it

        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QCOMPARE(loadSimulatedStatus(plan.source()).entries.size(), 1);
    }

    void refusesToExecuteAfterHeadMoved()
    {
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QVERIFY(plan.add(entry(PlanEntry::StagePaths, {u"f.txt"_s}), error));

        git({"commit", "-q", "--allow-empty", "-m", "Elsewhere"});
        const QByteArray realState = repoState();
        QVERIFY(!plan.execute(error));
        QVERIFY(error.contains("HEAD moved"_L1));
        QCOMPARE(repoState(), realState);
    }

    void refusesToExecuteAfterIndexChanged()
    {
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        write("g.txt", "g\n");
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QVERIFY(plan.add(entry(PlanEntry::StagePaths, {u"f.txt"_s}), error));

        git({"add", "g.txt"});
        const QByteArray realState = repoState();
        QVERIFY(!plan.execute(error));
        QVERIFY(error.contains("index"_L1));
        QCOMPARE(repoState(), realState);
        QVERIFY(!QFile::exists(m_dir->filePath(".git/index.lock")));
    }

    void removingAnEntryReplaysTheRest()
    {
        write("f.txt", lines({{2, "two"}, {5, "five"}, {9, "nine (B)"}}));
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QVERIFY(plan.add(patchEntry(plan, {u"2"_s, u"two"_s}), error));
        QVERIFY(plan.add(patchEntry(plan, {u"5"_s, u"five"_s}), error));
        QCOMPARE(simulatedIndex(plan, "f.txt"), lines({{2, "two"}, {5, "five"}, {9, "nine (B)"}}));

        plan.remove(plan.entries().first().id);
        QCOMPARE(plan.entries().size(), 1);
        QVERIFY(plan.executable());
        QCOMPARE(simulatedIndex(plan, "f.txt"), lines({{5, "five"}, {9, "nine (B)"}}));
    }

    void foldsIntoPlannedCommitsAndNoticesWhenTheyGo()
    {
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QVERIFY(plan.add(patchEntry(plan, {u"2"_s, u"two"_s}), error));
        PlanEntry commit = entry(PlanEntry::Commit);
        commit.message = u"C"_s;
        QVERIFY(plan.add(commit, error));
        const QString planned = plan.head();

        // Fold a later change into the commit the plan created.
        write("f.txt", lines({{2, "two"}, {3, "three"}, {9, "nine (B)"}}));
        PlanEntry fold = patchEntry(plan, {u"3"_s, u"three"_s});
        fold.kind = PlanEntry::Fold;
        fold.target = planned;
        QVERIFY2(plan.add(fold, error), qPrintable(error));
        QVERIFY(plan.head() != planned);
        QVERIFY(git({"show", plan.head(), "--format="}).contains("+three"));

        // Without the commit, the fold has nowhere to go.
        plan.remove(plan.entries()[1].id);
        QVERIFY(!plan.entries().last().error.isEmpty());
        QVERIFY(!plan.executable());
        QVERIFY(!plan.execute(error));
    }

    void foldsIntoRealCommitsAndPublishes()
    {
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        PlanEntry fold = patchEntry(plan, {u"2"_s, u"two"_s});
        fold.kind = PlanEntry::Fold;
        fold.target = rev("HEAD~1");
        QVERIFY2(plan.add(fold, error), qPrintable(error));
        QCOMPARE(git({"show", "HEAD~1:f.txt"}), lines({}));

        QVERIFY2(plan.execute(error), qPrintable(error));
        QCOMPARE(git({"show", "HEAD~1:f.txt"}), lines({{2, "two"}}));
        QVERIFY(git({"status", "--porcelain"}).isEmpty());
    }

    void replaysStagedFilesAsRecorded()
    {
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        write("g.txt", "g\n");
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QVERIFY(plan.add(entry(PlanEntry::StagePaths, {u"g.txt"_s}), error));
        QVERIFY(plan.add(entry(PlanEntry::StagePaths, {u"f.txt"_s}), error));

        // The file changes after it was staged; a replay keeps what was staged.
        write("f.txt", lines({{2, "two, edited later"}, {9, "nine (B)"}}));
        plan.remove(plan.entries().first().id);
        QCOMPARE(simulatedIndex(plan, "f.txt"), lines({{2, "two"}, {9, "nine (B)"}}));
        QCOMPARE(simulatedIndex(plan, "g.txt"), QByteArray());
    }

    void unstagesInThePlan()
    {
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        git({"add", "f.txt"});
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QVERIFY(plan.add(entry(PlanEntry::UnstagePaths, {u"f.txt"_s}), error));
        const StatusInfo simulated = loadSimulatedStatus(plan.source());
        QCOMPARE(simulated.entries.size(), 1);
        QVERIFY(!simulated.entries.first().hasStagedChange());
        QVERIFY(simulated.entries.first().hasUnstagedChange());

        QVERIFY(plan.execute(error));
        QVERIFY(git({"diff", "--cached"}).isEmpty());
        QVERIFY(git({"diff"}).contains("+two"));
    }

    void runsCommitHooksWhilePlanning()
    {
        hook("commit-msg", "#!/bin/sh\nprintf '\\nChange-Id: I123\\n' >> \"$1\"\n");
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QVERIFY(plan.add(entry(PlanEntry::StagePaths, {u"f.txt"_s}), error));
        PlanEntry commit = entry(PlanEntry::Commit);
        commit.message = u"C"_s;
        QVERIFY2(plan.add(commit, error), qPrintable(error));
        QVERIFY(git({"log", "-1", "--format=%B", plan.head()}).contains("Change-Id: I123"));

        hook("pre-commit", "#!/bin/sh\necho 'lint failed' >&2\nexit 1\n");
        const QString head = plan.head();
        write("f.txt", lines({{2, "two"}, {3, "three"}, {9, "nine (B)"}}));
        QVERIFY(plan.add(entry(PlanEntry::StagePaths, {u"f.txt"_s}), error));
        commit.message = u"D"_s;
        QVERIFY(!plan.add(commit, error));
        QVERIFY(error.contains("lint failed"_L1));
        QCOMPARE(plan.head(), head);
        QCOMPARE(plan.entries().size(), 3);
    }

    void amendsInThePlan()
    {
        write("f.txt", lines({{2, "two"}, {9, "nine (B)"}}));
        PlanSession plan;
        QString error;
        QVERIFY(plan.begin(m_git, error));
        QVERIFY(plan.add(entry(PlanEntry::StagePaths, {u"f.txt"_s}), error));
        PlanEntry amend = entry(PlanEntry::Commit);
        amend.amend = true;
        amend.message = u"B, amended"_s;
        QVERIFY2(plan.add(amend, error), qPrintable(error));
        QCOMPARE(git({"rev-parse", plan.head() + u"~1"_s}).trimmed(), rev("HEAD~1").toLatin1());
        QCOMPARE(git({"log", "-1", "--format=%s", plan.head()}), "B, amended\n"_ba);
    }

private:
    static QByteArray lines(const QHash<int, QByteArray> &changes)
    {
        QByteArray text;
        for (int i = 1; i <= 10; ++i)
            text += changes.value(i, QByteArray::number(i)) + '\n';
        return text;
    }

    static PlanEntry entry(PlanEntry::Kind kind, const QStringList &paths = {})
    {
        PlanEntry e;
        e.kind = kind;
        e.paths = paths;
        e.title = u"test"_s;
        return e;
    }

    // Stages the changed lines with the given texts, against the plan's
    // simulated index.
    PlanEntry patchEntry(const PlanSession &plan, const QStringList &selected)
    {
        const QList<FileDiff> files = parseDiff(plan.source().git.run(
            QStringList{u"diff"_s} + diffFormatArgs()).out);
        PlanEntry e = entry(PlanEntry::ApplyPatch);
        for (const FileDiff &file : files) {
            LineSelection selection;
            for (const DiffHunk &hunk : file.hunks) {
                QList<bool> hunkSelection;
                for (const DiffLine &line : hunk.lines)
                    hunkSelection.append(line.kind != DiffLine::Context
                                         && selected.contains(QString::fromUtf8(line.content)));
                selection.append(hunkSelection);
            }
            e.patch += buildPatch(file, selection, PatchDirection::Forward);
        }
        return e;
    }

    QByteArray simulatedIndex(const PlanSession &plan, const QString &path)
    {
        return plan.source().git.run({u"show"_s, u":"_s + path}).out;
    }

    // HEAD, the index and the working tree, to compare before and after.
    QByteArray repoState()
    {
        return git({"rev-parse", "HEAD"}) + git({"ls-files", "-s"}) + git({"status", "--porcelain"});
    }

    void hook(const QString &name, const QByteArray &script)
    {
        write(u".git/hooks/"_s + name, script);
        QVERIFY(QFile::setPermissions(m_dir->filePath(u".git/hooks/"_s + name),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }

    QByteArray git(const QStringList &args)
    {
        return m_git.run(args).out;
    }

    QString rev(const QString &r) { return QString::fromLatin1(git({"rev-parse", r}).trimmed()); }

    void write(const QString &name, const QByteArray &content)
    {
        QFile file(m_dir->filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    GitRunner m_git;
};

QTEST_GUILESS_MAIN(TestPlanSession)
#include "tst_plansession.moc"
