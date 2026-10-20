#include "core/DiffParser.h"
#include "core/FoldEngine.h"
#include "core/PatchBuilder.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestFoldEngine : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Default");
        qputenv("GIT_AUTHOR_EMAIL", "default@example.com");
        qputenv("GIT_COMMITTER_NAME", "Committer");
        qputenv("GIT_COMMITTER_EMAIL", "committer@example.com");
    }

    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_git.setWorkDir(m_dir->path());
        git({"init", "-q", "-b", "main"});

        // A: app.txt with ten lines; B: notes.txt; C: changes line 9.
        write("app.txt", lines({}));
        commitAll("A: add app\n\nChange-Id: I0000000000000000000000000000000000000001\n",
                  "Alice", "1600000000 +0100");
        write("notes.txt", "notes\n");
        commitAll("B: add notes\n", "Bob", "1600001000 +0200");
        write("app.txt", lines({{9, "nine, revised"}}));
        commitAll("C: revise nine\n", "Carol", "1600002000 -0500");
    }

    void foldsUnstagedLinesIntoOlderCommit()
    {
        write("app.txt", lines({{2, "two, fixed"}, {9, "nine, revised"}, {5, "five, not folded"}}));
        const QByteArray before = git({"log", "--format=%an %ad %s%n%b", "--date=raw"});

        FoldEngine engine(m_git);
        const FoldResult result = engine.fold(rev("HEAD~2"), unstagedPatch({"2", "two, fixed"}),
                                              FoldBase::Index);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.rewritten, 3);
        QCOMPARE(rev("HEAD"), result.newHead);

        // Same commits, authors, dates and messages; only the content moved.
        QCOMPARE(git({"log", "--format=%an %ad %s%n%b", "--date=raw"}), before);
        QVERIFY(git({"show", "HEAD~2", "--format="}).contains("+two, fixed"));
        QVERIFY(!git({"show", "HEAD~1", "--format="}).contains("two"));
        QCOMPARE(git({"show", "HEAD:app.txt"}), lines({{2, "two, fixed"}, {9, "nine, revised"}}));
        QCOMPARE(git({"log", "-1", "--format=%cn", "HEAD~2"}), "Committer\n"_ba);

        // The folded line is neither staged nor unstaged any more; the other
        // change is still unstaged and the working tree is untouched.
        QVERIFY(git({"diff", "--cached"}).isEmpty());
        const QByteArray unstaged = git({"diff"});
        QVERIFY(unstaged.contains("+five, not folded"));
        QVERIFY(!unstaged.contains("+two"));
        QCOMPARE(read("app.txt"), lines({{2, "two, fixed"}, {9, "nine, revised"}, {5, "five, not folded"}}));
        QVERIFY(result.indexUpdated);
    }

    void foldsStagedLinesAndKeepsIndex()
    {
        write("app.txt", lines({{1, "one, staged"}, {9, "nine, revised"}, {3, "three, staged too"}}));
        git({"add", "app.txt"});
        const QByteArray indexTree = git({"write-tree"});

        FoldEngine engine(m_git);
        const FoldResult result = engine.fold(rev("HEAD~1"),
                                              stagedPatch({"1", "one, staged"}), FoldBase::Head);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(git({"write-tree"}), indexTree);
        QVERIFY(git({"show", "HEAD~1:app.txt"}).contains("one, staged"));

        const QByteArray staged = git({"diff", "--cached"});
        QVERIFY(staged.contains("+three, staged too"));
        QVERIFY(!staged.contains("+one"));
        QVERIFY(!staged.contains("-1\n"));
    }

    void foldsIntoRootCommit()
    {
        // Not line 10: changes touching lines that later commits changed
        // conflict, as they would in a rebase.
        write("app.txt", lines({{6, "six"}, {9, "nine, revised"}}));
        FoldEngine engine(m_git);
        const FoldResult result = engine.fold(rev("HEAD~2"), unstagedPatch({"6", "six"}),
                                              FoldBase::Index);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(git({"rev-list", "--parents", "-1", "HEAD~2"}).trimmed().split(' ').size() == 1);
        QVERIFY(git({"show", "HEAD~2:app.txt"}).contains("six"));
        QVERIFY(git({"status", "--porcelain"}).isEmpty());
    }

    void foldsNewFile()
    {
        write("extra.txt", "first\nsecond\n");
        FoldEngine engine(m_git);
        const FoldResult result = engine.fold(rev("HEAD~1"), untrackedPatch("extra.txt", {"first"}),
                                              FoldBase::Index);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(git({"show", "HEAD~1:extra.txt"}), "first\n"_ba);
        QCOMPARE(git({"show", ":extra.txt"}), "first\n"_ba);
        QVERIFY(git({"diff"}).contains("+second"));
    }

    void conflictLeavesEverythingUntouched()
    {
        // Line 9 as changed by C cannot be folded into B, which predates it.
        write("app.txt", lines({{9, "nine, revised again"}}));
        const QString head = rev("HEAD");
        const QByteArray status = git({"status", "--porcelain"});

        FoldEngine engine(m_git);
        const FoldResult result = engine.fold(rev("HEAD~1"),
                                              unstagedPatch({"nine, revised", "nine, revised again"}),
                                              FoldBase::Index);
        QVERIFY(!result.ok);
        QCOMPARE(result.conflicts, QStringList{u"app.txt"_s});
        QCOMPARE(rev("HEAD"), head);
        QCOMPARE(git({"status", "--porcelain"}), status);
    }

    void foldIntoHeadWorksLikeAmend()
    {
        write("app.txt", lines({{4, "four"}, {9, "nine, revised"}}));
        FoldEngine engine(m_git);
        const FoldResult result = engine.fold(rev("HEAD"), unstagedPatch({"4", "four"}), FoldBase::Index);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.rewritten, 1);
        QCOMPARE(git({"log", "-1", "--format=%s"}), "C: revise nine\n"_ba);
        QVERIFY(git({"status", "--porcelain"}).isEmpty());
    }

    void blocksCommitsOffTheBranch()
    {
        git({"checkout", "-q", "-b", "side", "HEAD~1"});
        write("side.txt", "side\n");
        commitAll("S: side\n", "Sam", "1600003000 +0000");
        git({"checkout", "-q", "main"});

        FoldEngine engine(m_git);
        QVERIFY(!engine.blocker(rev("side")).isEmpty());
        QVERIFY(engine.blocker(rev("HEAD~2")).isEmpty());

        git({"merge", "-q", "--no-ff", "side", "-m", "Merge side"});
        QVERIFY(!engine.blocker(rev("HEAD~2")).isEmpty());
        QVERIFY(engine.blocker(rev("HEAD")).isEmpty());
    }

private:
    static QByteArray lines(const QHash<int, QByteArray> &changes)
    {
        QByteArray text;
        for (int i = 1; i <= 10; ++i)
            text += changes.value(i, QByteArray::number(i)) + '\n';
        return text;
    }

    QByteArray git(const QStringList &args, const QProcessEnvironment &env = {})
    {
        const GitResult result = m_git.run(args, {}, env);
        if (!result.ok() && args.first() != "diff"_L1)
            qWarning() << args << result.errorText();
        return result.out;
    }

    QString rev(const QString &r) { return QString::fromLatin1(git({"rev-parse", r}).trimmed()); }

    void write(const QString &name, const QByteArray &content)
    {
        QFile file(m_dir->filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    QByteArray read(const QString &name)
    {
        QFile file(m_dir->filePath(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    void commitAll(const QString &message, const QString &author, const QString &date)
    {
        QProcessEnvironment env;
        env.insert(u"GIT_AUTHOR_NAME"_s, author);
        env.insert(u"GIT_AUTHOR_EMAIL"_s, author.toLower() + u"@example.com"_s);
        env.insert(u"GIT_AUTHOR_DATE"_s, date);
        git({"add", "-A"});
        git({"commit", "-q", "--cleanup=verbatim", "-m", message}, env);
    }

    // Patch of the changed lines whose content is in `selected`.
    QByteArray patchOf(const QStringList &diffArgs, const QStringList &selected)
    {
        const QList<FileDiff> files = parseDiff(git(QStringList{"diff", "--no-color", "--src-prefix=a/",
                                                                "--dst-prefix=b/"} + diffArgs));
        QByteArray patch;
        for (const FileDiff &file : files) {
            LineSelection selection;
            for (const DiffHunk &hunk : file.hunks) {
                QList<bool> hunkSelection;
                for (const DiffLine &line : hunk.lines)
                    hunkSelection.append(line.kind != DiffLine::Context
                                         && selected.contains(QString::fromUtf8(line.content)));
                selection.append(hunkSelection);
            }
            patch += buildPatch(file, selection, PatchDirection::Forward);
        }
        return patch;
    }

    QByteArray unstagedPatch(const QStringList &selected) { return patchOf({}, selected); }
    QByteArray stagedPatch(const QStringList &selected) { return patchOf({"--cached"}, selected); }
    QByteArray untrackedPatch(const QString &path, const QStringList &selected)
    {
        return patchOf({"--no-index", "--", "/dev/null", path}, selected);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    GitRunner m_git;
};

QTEST_GUILESS_MAIN(TestFoldEngine)
#include "tst_foldengine.moc"
