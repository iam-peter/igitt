#include "Repository.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

// Waits until every queued git job has run and its result is applied.
[[nodiscard]] bool settle(Repository &repo)
{
    return QTest::qWaitFor([&repo] { return !repo.busy(); }, 10000);
}

} // namespace

class TestRepository : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keep the user's configuration (signing, hooks, aliases) out of it.
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
        writeFile(file(), "one\ntwo\nthree\nfour\nfive\n");
        git({"add", "f.txt"});
        git({"commit", "-q", "-m", "First"});
        writeFile(m_dir->filePath("g.txt"), "g\n");
        git({"add", "g.txt"});
        git({"commit", "-q", "-m", "Second"});
        m_repo.reset(new Repository);
        m_errors.reset(new QSignalSpy(m_repo.get(), &Repository::errorOccurred));
    }

    void cleanup()
    {
        if (m_errors && !m_errors->isEmpty())
            qWarning() << "errors reported:" << *m_errors;
        m_errors.reset();
        m_repo.reset();
    }

    void opensHistoryAndStatus()
    {
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        writeFile(m_dir->filePath("new.txt"), "new\n");

        // Any directory inside the work tree will do.
        QVERIFY(QDir(m_dir->path()).mkdir(u"sub"_s));
        open(m_dir->filePath(u"sub"_s));
        QCOMPARE(repo().path(), QDir(m_dir->path()).canonicalPath());
        QCOMPARE(repo().branch(), u"main"_s);
        QCOMPARE(repo().commits()->rowCount(), 2);
        QCOMPARE(repo().commits()->data(repo().commits()->index(0), CommitModel::SubjectRole).toString(),
                 u"Second"_s);
        QCOMPARE(repo().headMessage(), u"Second"_s);
        QCOMPARE(repo().unstaged()->count(), 2);
        QCOMPARE(repo().staged()->count(), 0);
        QVERIFY(m_errors->isEmpty());
    }

    void rejectsNonRepository()
    {
        QTemporaryDir plain;
        repo().open(plain.path());
        QVERIFY(settle(repo()));
        QCOMPARE(m_errors->size(), 1);
        QVERIFY(!repo().valid());
        m_errors->clear();
    }

    void gitRunsOffTheUiThread()
    {
        // A slow hook would freeze a UI that waits for git.
        writeFile(m_dir->filePath(".git/hooks/pre-commit"), "#!/bin/sh\nsleep 1\n");
        QVERIFY(QFile::setPermissions(m_dir->filePath(".git/hooks/pre-commit"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        open(m_dir->path());
        repo().stageFile(u"f.txt"_s);

        QSignalSpy committed(&repo(), &Repository::committed);
        QElapsedTimer timer;
        timer.start();
        repo().commit(u"Slow"_s, false);
        QVERIFY(timer.elapsed() < 200);
        QVERIFY(repo().busy());
        QVERIFY(repo().writing());

        QVERIFY(settle(repo()));
        QCOMPARE(committed.size(), 1);
        QCOMPARE(repo().headMessage(), u"Slow"_s);
    }

    void stagesSelectedLines()
    {
        writeFile(file(), "one\nTWO\nthree\nFOUR\nfive\n");
        open(m_dir->path());

        showFile(u"f.txt"_s, false);
        selectLinesContaining({u"two"_s, u"TWO"_s});
        QCOMPARE(repo().diff()->selectedCount(), 2);
        repo().applySelection();
        QVERIFY(settle(repo()));

        QVERIFY(git({"diff", "--cached"}).contains("+TWO"));
        QVERIFY(!git({"diff", "--cached"}).contains("FOUR"));
        QVERIFY(git({"diff"}).contains("+FOUR"));
        QVERIFY(!git({"diff"}).contains("+TWO"));
        QCOMPARE(repo().staged()->count(), 1);
        // The diff on display reloaded; TWO is context now.
        QVERIFY(isChange(u"FOUR"_s));
        QVERIFY(rowOf(u"TWO"_s) >= 0);
        QVERIFY(!isChange(u"TWO"_s));
    }

    void unstagesSelectedLines()
    {
        writeFile(file(), "one\nTWO\nthree\nFOUR\nfive\n");
        git({"add", "f.txt"});
        open(m_dir->path());

        showFile(u"f.txt"_s, true);
        selectLinesContaining({u"four"_s, u"FOUR"_s});
        repo().applySelection();
        QVERIFY(settle(repo()));

        QVERIFY(git({"diff", "--cached"}).contains("+TWO"));
        QVERIFY(!git({"diff", "--cached"}).contains("FOUR"));
        QVERIFY(git({"diff"}).contains("+FOUR"));
    }

    void stagesHunkAndLine()
    {
        // Far enough apart for two hunks.
        const QByteArray middle = "2\n3\n4\n5\n6\n7\n8\n9\n10\n";
        writeFile(file(), "ONE\n" + middle + "ELEVEN\nTWELVE\n");
        git({"commit", "-q", "-a", "-m", "Longer"});
        writeFile(file(), "one\n" + middle + "eleven\ntwelve\n");
        open(m_dir->path());
        showFile(u"f.txt"_s, false);

        // Stage Hunk from the context menu, on a context line of hunk two.
        repo().applyHunk(rowOf(u"9"_s));
        QVERIFY(settle(repo()));
        QCOMPARE(git({"show", ":f.txt"}), "ONE\n" + middle + "eleven\ntwelve\n");

        // Stage Line on the added line alone: the removed one stays.
        repo().applyLine(rowOf(u"one"_s));
        QVERIFY(settle(repo()));
        QCOMPARE(git({"show", ":f.txt"}), "ONE\none\n" + middle + "eleven\ntwelve\n");
    }

    void stagesPartOfUntrackedFile()
    {
        writeFile(m_dir->filePath("new.txt"), "keep\nlater\n");
        open(m_dir->path());

        showFile(u"new.txt"_s, false);
        selectLinesContaining({u"keep"_s});
        repo().applySelection();
        QVERIFY(settle(repo()));
        QCOMPARE(git({"show", ":new.txt"}), "keep\n"_ba);
    }

    void stagesPartOfDeletion()
    {
        QFile::remove(file());
        open(m_dir->path());

        showFile(u"f.txt"_s, false);
        selectLinesContaining({u"one"_s});
        repo().applySelection();
        QVERIFY(settle(repo()));
        QCOMPARE(git({"show", ":f.txt"}), "two\nthree\nfour\nfive\n"_ba);
    }

    void commitsAndAmends()
    {
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        open(m_dir->path());
        repo().stageFile(u"f.txt"_s);
        repo().commit(u"Third"_s, false);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().commits()->rowCount(), 3);
        QCOMPARE(repo().headMessage(), u"Third"_s);

        repo().commit(u"Third, reworded"_s, true);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().commits()->rowCount(), 3);
        QCOMPARE(repo().headMessage(), u"Third, reworded"_s);
        QCOMPARE(repo().staged()->count(), 0);
    }

    void failedCommitIsReported()
    {
        open(m_dir->path());
        QSignalSpy committed(&repo(), &Repository::committed);
        repo().commit(u"Nothing staged"_s, false);
        QVERIFY(settle(repo()));
        QCOMPARE(committed.size(), 0);
        QCOMPARE(m_errors->size(), 1);
        m_errors->clear();
    }

    void dropsResultsForViewsLeftMeanwhile()
    {
        open(m_dir->path());
        // Queued back to back; only the last view may end up on screen.
        repo().showCommit(repo().commits()->shaAt(1));
        repo().showCommit(repo().commits()->shaAt(0));
        QVERIFY(settle(repo()));
        QCOMPARE(repo().diffCommit(), repo().commits()->shaAt(0));
        QVERIFY(repo().commitDetails().contains("Second"_L1));
        QVERIFY(rowOf(u"g"_s) >= 0);
    }

    void foldsSelectionAndUndoes()
    {
        writeFile(file(), "one\nTWO\nthree\nFOUR\nfive\n");
        const QByteArray oldHead = git({"rev-parse", "HEAD"});
        open(m_dir->path());
        const QString first = repo().commits()->shaAt(1);

        QVERIFY(!repo().foldBlocker(first).isEmpty()); // nothing selected yet
        showFile(u"f.txt"_s, false);
        selectLinesContaining({u"two"_s, u"TWO"_s});
        QVERIFY(repo().foldBlocker(first).isEmpty());
        QVERIFY(!repo().isPublished(first));

        QSignalSpy notices(&repo(), &Repository::notice);
        repo().foldSelection(first);
        QVERIFY(settle(repo()));
        QCOMPARE(notices.size(), 1);
        QVERIFY(repo().canUndoFold());
        QCOMPARE(repo().commits()->rowCount(), 2);
        QVERIFY(git({"show", "HEAD~1:f.txt"}).contains("TWO"));
        QVERIFY(git({"diff"}).contains("+FOUR"));
        QVERIFY(!git({"diff"}).contains("+TWO"));
        QVERIFY(git({"diff", "--cached"}).isEmpty());

        repo().undoFold();
        QVERIFY(settle(repo()));
        QVERIFY(!repo().canUndoFold());
        QCOMPARE(git({"rev-parse", "HEAD"}), oldHead);
        QVERIFY(git({"diff"}).contains("+TWO"));
        QVERIFY(git({"diff", "--cached"}).isEmpty());
    }

    void knowsFoldTargetsAndPublishedCommits()
    {
        git({"update-ref", "refs/remotes/origin/main", "HEAD~1"});
        git({"checkout", "-q", "-b", "side", "HEAD~1"});
        writeFile(m_dir->filePath("side.txt"), "side\n");
        git({"add", "side.txt"});
        git({"commit", "-q", "-m", "Side"});
        git({"checkout", "-q", "main"});
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        open(m_dir->path());
        showFile(u"f.txt"_s, false);
        selectLinesContaining({u"two"_s, u"TWO"_s});

        const auto sha = [this](const char *rev) { return QString::fromLatin1(git({"rev-parse", rev}).trimmed()); };
        QVERIFY(repo().foldBlocker(sha("main")).isEmpty());
        QVERIFY(!repo().isPublished(sha("main")));
        QVERIFY(repo().foldBlocker(sha("main~1")).isEmpty());
        QVERIFY(repo().isPublished(sha("main~1")));
        QVERIFY(!repo().foldBlocker(sha("side")).isEmpty());
    }

    void suggestsFoldTarget()
    {
        writeFile(m_dir->filePath("g.txt"), "G\n");
        open(m_dir->path());
        showFile(u"g.txt"_s, false);
        QVERIFY(repo().suggestedFoldTarget().isEmpty());

        selectLinesContaining({u"g"_s, u"G"_s});
        QCOMPARE(repo().suggestedFoldTarget(), repo().commits()->shaAt(0));
        QVERIFY(repo().foldSuggestionText().contains("Second"_L1));

        repo().diff()->clearSelection();
        QVERIFY(repo().suggestedFoldTarget().isEmpty());
        QVERIFY(repo().foldSuggestionText().isEmpty());
    }

    void plansAndExecutes()
    {
        writeFile(file(), "one\nTWO\nthree\nFOUR\nfive\n");
        const QByteArray realHead = git({"rev-parse", "HEAD"});
        open(m_dir->path());
        repo().startPlanning();
        QVERIFY(settle(repo()));
        QVERIFY(repo().planning());

        showFile(u"f.txt"_s, false);
        selectLinesContaining({u"two"_s, u"TWO"_s});
        repo().applySelection();
        QVERIFY(settle(repo()));
        QCOMPARE(repo().plan()->count(), 1);
        QCOMPARE(repo().staged()->count(), 1); // simulated
        QVERIFY(git({"diff", "--cached"}).isEmpty()); // real

        QSignalSpy committed(&repo(), &Repository::committed);
        repo().commit(u"Third"_s, false);
        QVERIFY(settle(repo()));
        QCOMPARE(committed.size(), 1);
        QCOMPARE(repo().plan()->count(), 2);
        QVERIFY(repo().plan()->executable());
        QCOMPARE(repo().commits()->rowCount(), 3);
        QVERIFY(repo().commits()->data(repo().commits()->index(0), CommitModel::PlannedRole).toBool());
        QVERIFY(!repo().commits()->data(repo().commits()->index(1), CommitModel::PlannedRole).toBool());
        QCOMPARE(repo().headMessage(), u"Third"_s);
        QCOMPARE(git({"rev-parse", "HEAD"}), realHead);
        // The review shows what ran.
        const auto log = [this](int row) {
            return repo().plan()->data(repo().plan()->index(row), PlanModel::LogRole).toString();
        };
        QVERIFY(log(0).startsWith("$ git apply --cached --3way"_L1));
        QVERIFY(log(0).contains("+TWO"_L1));
        QVERIFY(log(1).contains("$ git hook run --ignore-missing commit-msg"_L1));
        QVERIFY(log(1).contains("$ git commit-tree "_L1));
        QVERIFY(repo().plan()->summary().contains("git update-ref"_L1));

        repo().executePlan();
        QVERIFY(settle(repo()));
        QVERIFY(!repo().planning());
        QCOMPARE(repo().plan()->count(), 0);
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Third\n"_ba);
        QVERIFY(git({"diff", "--cached"}).isEmpty());
        QVERIFY(git({"diff"}).contains("+FOUR"));
        QVERIFY(!repo().commits()->data(repo().commits()->index(0), CommitModel::PlannedRole).toBool());
    }

    void discardsPlan()
    {
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        open(m_dir->path());
        repo().startPlanning();
        repo().stageFile(u"f.txt"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().staged()->count(), 1);

        repo().discardPlan();
        QVERIFY(settle(repo()));
        QVERIFY(!repo().planning());
        QCOMPARE(repo().staged()->count(), 0);
        QVERIFY(git({"diff", "--cached"}).isEmpty());
    }

    void showsUncommittedChangesOnTop()
    {
        open(m_dir->path());
        QVERIFY(!repo().hasWip());

        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        writeFile(m_dir->filePath("new.txt"), "new\n");
        repo().refreshStatus();
        QVERIFY(settle(repo()));
        QVERIFY(repo().hasWip());
        QCOMPARE(repo().wipCounts().value(u"modified"_s).toInt(), 1);
        QCOMPARE(repo().wipCounts().value(u"added"_s).toInt(), 1);

        git({"checkout", "--", "f.txt"});
        QFile::remove(m_dir->filePath("new.txt"));
        repo().refreshStatus();
        QVERIFY(settle(repo()));
        QVERIFY(!repo().hasWip());
    }

    void loadsCommitStatsInBatches()
    {
        open(m_dir->path());
        const QString second = repo().commits()->shaAt(0);
        const QString first = repo().commits()->shaAt(1);
        QVERIFY(!repo().commits()->data(repo().commits()->index(0), CommitModel::StatsRole).isValid());

        repo().requestStats(second);
        repo().requestStats(first);
        QTRY_VERIFY(repo().commits()->hasStats(first) && repo().commits()->hasStats(second));
        const QVariantMap stats = repo().commits()->data(repo().commits()->index(1), CommitModel::StatsRole).toMap();
        QCOMPARE(stats.value(u"files"_s).toInt(), 1);
        QCOMPARE(stats.value(u"added"_s).toInt(), 5);
        QVERIFY(settle(repo()));
    }

    // An untracked file ignored, shared or only here
    void ignoresFiles()
    {
        QCOMPARE(repo().ignorePatterns(u"build/out.log"_s),
                 (QStringList{u"/build/out.log"_s, u"*.log"_s, u"/build/"_s, u"out.log"_s}));
        QCOMPARE(repo().ignorePatterns(u"tmp/"_s), QStringList{u"/tmp/"_s});
        QCOMPARE(repo().ignorePatterns(u"a*b?.txt"_s).first(), u"/a\\*b\\?.txt"_s);
        QCOMPARE(repo().ignorePatterns(u"!x"_s).first(), u"/\\!x"_s);

        QDir(m_dir->path()).mkpath(u"build"_s);
        writeFile(m_dir->filePath(u"build/out.log"_s), "log\n");
        writeFile(m_dir->filePath(u"scratch.txt"_s), "scratch\n");
        writeFile(m_dir->filePath(u".gitignore"_s), "*.o"); // no newline at the end
        git({"add", ".gitignore"});
        git({"commit", "-q", "-m", "Ignore objects"});
        open(m_dir->path());
        repo().ignore(u"*.log"_s, false);
        QVERIFY(settle(repo()));
        QCOMPARE(readFile(m_dir->filePath(u".gitignore"_s)), "*.o\n*.log\n"_ba);
        repo().ignore(u"/scratch.txt"_s, true);
        QVERIFY(settle(repo()));
        QVERIFY(readFile(m_dir->filePath(u".git/info/exclude"_s)).endsWith("/scratch.txt\n"));
        const QByteArray status = git({"status", "--porcelain"});
        QVERIFY(!status.contains("out.log"));
        QVERIFY(!status.contains("scratch.txt"));
        QVERIFY(status.contains(".gitignore"));
        bool listed = false;
        for (int row = 0; row < repo().unstaged()->rowCount(); ++row)
            listed |= repo().unstaged()->data(repo().unstaged()->index(row), FileListModel::PathRole).toString()
                      == u"scratch.txt"_s;
        QVERIFY(!listed);
    }

    // code: finds the commits that added or removed a text
    void searchesCode()
    {
        writeFile(file(), "one\ntwo\nTHREE\nfour\nfive\n");
        git({"commit", "-q", "-am", "Shout"});
        open(m_dir->path());
        CommitModel *commits = repo().commits();
        const auto subjects = [commits] {
            QStringList found;
            for (int row = 0; row < commits->count(); ++row) {
                if (commits->matches(row))
                    found << commits->subjectAt(row);
            }
            return found;
        };
        const auto search = [commits](const QString &query, bool caseSensitive, bool regex) {
            commits->setSearch(query, caseSensitive, regex);
            return QTest::qWaitFor([commits] { return !commits->searchingCode(); }, 10000);
        };
        QVERIFY(search(u"code:three"_s, true, false));
        QCOMPARE(subjects(), (QStringList{u"Shout"_s, u"First"_s}));
        QVERIFY(search(u"code:THREE"_s, true, false));
        QCOMPARE(subjects(), QStringList{u"Shout"_s});
        // Ignoring case, "three" is in the file as often after Shout as before
        QVERIFY(search(u"code:three"_s, false, false));
        QCOMPARE(subjects(), QStringList{u"First"_s});
        // -G: a changed line matches
        QVERIFY(search(u"code:thr+ee"_s, false, true));
        QCOMPARE(subjects(), (QStringList{u"Shout"_s, u"First"_s}));
        // Together with the other terms
        QVERIFY(search(u"code:three message:Shout"_s, true, false));
        QCOMPARE(subjects(), QStringList{u"Shout"_s});
        // Known after a refresh too
        repo().refresh();
        QVERIFY(settle(repo()));
        QTRY_VERIFY(!commits->searchingCode());
        QCOMPARE(subjects(), QStringList{u"Shout"_s});
        commits->setSearch({});
    }

    void searchesAndFiltersHistory()
    {
        git({"tag", "v1", "HEAD~1"});
        git({"update-ref", "refs/remotes/origin/main", "HEAD"});
        open(m_dir->path());
        CommitModel *commits = repo().commits();
        commits->setSearch(u"sec"_s);
        QCOMPARE(commits->matchCount(), 1);
        commits->setSearch(u"first"_s);
        QCOMPARE(commits->findNext(-1), 1);
        commits->setSearch(u"test"_s); // by author
        QCOMPARE(commits->matchCount(), 2);
        QCOMPARE(commits->findNext(1), 0); // wrapping
        commits->setSearch(u"sha:"_s + commits->shaAt(1).left(7));
        QCOMPARE(commits->findNext(1), 1);
        commits->setSearch(u"author:test message:sec"_s);
        QCOMPARE(commits->matchCount(), 1);
        commits->setSearch(u"author:nobody"_s);
        QCOMPARE(commits->matchCount(), 0);
        commits->setSearch(u"\"irst\""_s);
        QCOMPARE(commits->matchCount(), 1);
        commits->setSearch(u"FIRST"_s, true);
        QCOMPARE(commits->matchCount(), 0);
        commits->setSearch(u"^S.c"_s, false, true);
        QCOMPARE(commits->matchCount(), 1);
        commits->setSearch(u"(unclosed"_s, false, true);
        QVERIFY(!commits->searchError().isEmpty());
        QVERIFY(!commits->data(commits->index(0), CommitModel::MatchedRole).toBool());
        commits->setSearch({});
        QVERIFY(!commits->searching());
        QVERIFY(commits->data(commits->index(0), CommitModel::MatchedRole).toBool());

        repo().setCurrentBranchOnly(true);
        QVERIFY(settle(repo()));
        QCOMPARE(commits->rowCount(), 2);

        // main and origin/main share a label; the tag has its own.
        const QVariantList headLabels = commits->data(commits->index(0), CommitModel::RefsRole).toList();
        QCOMPARE(headLabels.size(), 1);
        QCOMPARE(headLabels.first().toMap().value(u"remotes"_s).toStringList(), QStringList{u"origin/main"_s});
        QCOMPARE(commits->data(commits->index(1), CommitModel::RefsRole).toList().size(), 1);

        repo().setShowTags(false);
        repo().setShowRemotes(false);
        QVERIFY(settle(repo()));
        QVERIFY(commits->data(commits->index(1), CommitModel::RefsRole).toList().isEmpty());
        QVERIFY(commits->data(commits->index(0), CommitModel::RefsRole).toList().first().toMap()
                    .value(u"remotes"_s).toStringList().isEmpty());
    }

    void listsTrackedFiles()
    {
        writeFile(m_dir->filePath("untracked.txt"), "x\n");
        open(m_dir->path());
        repo().loadFiles();
        QVERIFY(settle(repo()));
        QCOMPARE(repo().files()->fileCount(), 2);
        QCOMPARE(repo().files()->match(u"g"_s), QStringList{u"g.txt"_s});
    }

    void showsFileContent()
    {
        writeFile(file(), "one\ntwo\nthree\nfour\nfive\nsix\n"); // unstaged edit
        open(m_dir->path());
        repo().showFileContent(u"f.txt"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().diffSource(), Repository::FileContent);
        DiffModel *diff = repo().diff();
        QCOMPARE(diff->rowCount(), 6); // the lines as on disk, without a file header
        QCOMPARE(diff->data(diff->index(0), DiffModel::KindRole).toInt(), int(DiffModel::Context));
        QCOMPARE(diff->data(diff->index(0), DiffModel::PathRole).toString(), u"f.txt"_s);
        QCOMPARE(diff->data(diff->index(5), DiffModel::TextRole).toString(), u"six"_s);
        QCOMPARE(diff->data(diff->index(5), DiffModel::NewLineRole).toInt(), 6);
        QVERIFY(!diff->data(diff->index(5), DiffModel::SelectableRole).toBool());

        // Deleted from disk: the staged version.
        QFile::remove(file());
        repo().showFileContent(u"f.txt"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(diff->rowCount(), 5);

        writeFile(m_dir->filePath("blob.bin"), QByteArray("\0\1\2", 3));
        repo().showFileContent(u"blob.bin"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(diff->data(diff->index(0), DiffModel::TextRole).toString(), u"Binary file"_s);
    }

    void ignoresWhitespace()
    {
        // TWO is a change; the indented line changed only in white space.
        writeFile(file(), "one\nTWO\n  three\nfour\nfive\n");
        open(m_dir->path());
        showFile(u"f.txt"_s, false);
        QVERIFY(isChange(u"  three"_s));
        selectLinesContaining({u"two"_s});
        QCOMPARE(repo().diff()->selectedCount(), 1);

        repo().setIgnoreWhitespace(true);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().diff()->selectedCount(), 0);
        QVERIFY(isChange(u"TWO"_s));
        QVERIFY(rowOf(u"  three"_s) >= 0);
        QVERIFY(!isChange(u"  three"_s));
        // Its lines would not apply; nothing is staged.
        selectLinesContaining({u"TWO"_s});
        repo().applySelection();
        QVERIFY(settle(repo()));
        QVERIFY(git({"diff", "--cached"}).isEmpty());

        // Commits too.
        git({"commit", "-q", "-a", "-m", "Indent"});
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().showCommit(repo().commits()->shaAt(0));
        QVERIFY(settle(repo()));
        QVERIFY(isChange(u"TWO"_s));
        QVERIFY(!isChange(u"  three"_s));

        // Nothing but white space changed: say so rather than show nothing.
        writeFile(file(), "one\nTWO\n\tthree\nfour\nfive\n");
        showFile(u"f.txt"_s, false);
        QCOMPARE(repo().diff()->rowCount(), 1);
        QCOMPARE(repo().diff()->data(repo().diff()->index(0), DiffModel::KindRole).toInt(), int(DiffModel::Message));

        repo().setIgnoreWhitespace(false);
        QVERIFY(settle(repo()));
        QVERIFY(isChange(u"    three"_s)); // the tab, expanded
    }

    void undoIsForgottenWhenHeadMoves()
    {
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        open(m_dir->path());
        showFile(u"f.txt"_s, false);
        selectLinesContaining({u"two"_s, u"TWO"_s});
        repo().foldSelection(repo().commits()->shaAt(1));
        QVERIFY(settle(repo()));
        QVERIFY(repo().canUndoFold());

        git({"commit", "-q", "--allow-empty", "-m", "Elsewhere"});
        repo().refreshStatus();
        QVERIFY(settle(repo()));
        QVERIFY(!repo().canUndoFold());
    }

    // A branch name as git's check-ref-format takes it, and not one taken.
    void checksBranchNames()
    {
        open(m_dir->path());
        git({"branch", "topic/one"});
        repo().refresh();
        QVERIFY(settle(repo()));
        QCOMPARE(repo().branches(), (QStringList{u"main"_s, u"topic/one"_s}));
        for (const QString &good : {u"feature"_s, u"fix/login-form"_s, u"v2.1"_s})
            QVERIFY2(repo().branchNameError(good).isEmpty(), qPrintable(good));
        for (const QString &bad : {u""_s, u"has space"_s, u"a..b"_s, u"x~1"_s, u"q?"_s, u"-flag"_s, u"HEAD"_s,
                                   u"ends/"_s, u"ends."_s, u".hidden"_s, u"a/.b"_s, u"x.lock"_s, u"at@{1}"_s,
                                   u"a//b"_s, u"main"_s, u"topic"_s, u"topic/one/two"_s})
            QVERIFY2(!repo().branchNameError(bad).isEmpty(), qPrintable(bad));
    }

    // Branches made, switched to, renamed and deleted; switching keeps the
    // uncommitted changes it can, and says which files are in the way.
    void makesAndSwitchesBranches()
    {
        open(m_dir->path());
        const QString first = QString::fromUtf8(git({"rev-parse", "HEAD~1"}).trimmed());
        repo().createBranch(u"old"_s, first, false);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().branch(), u"main"_s);
        QCOMPARE(git({"rev-parse", "old"}).trimmed(), first.toUtf8());
        QVERIFY(repo().branches().contains(u"old"_s));

        repo().createBranch(u"work"_s, {}, true);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().branch(), u"work"_s);

        // An uncommitted change git can carry over comes along
        writeFile(m_dir->filePath("g.txt"), "g changed\n");
        repo().checkoutBranch(u"main"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().branch(), u"main"_s);
        QCOMPARE(repo().unstaged()->count(), 1);

        // One the other branch would overwrite keeps it where it is
        repo().checkoutBranch(u"old"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().branch(), u"main"_s);
        QCOMPARE(m_errors->size(), 1);
        QVERIFY(m_errors->takeFirst().first().toString().contains(u"g.txt"_s));
        git({"checkout", "--", "g.txt"});

        repo().renameBranch(u"work"_s, u"done"_s);
        QVERIFY(settle(repo()));
        QVERIFY(repo().branches().contains(u"done"_s));
        QVERIFY(!repo().branches().contains(u"work"_s));

        // Not merged: kept until forced
        repo().checkoutBranch(u"done"_s);
        QVERIFY(settle(repo()));
        writeFile(m_dir->filePath("h.txt"), "h\n");
        git({"add", "h.txt"});
        git({"commit", "-q", "-m", "Third"});
        repo().checkoutBranch(u"main"_s);
        QVERIFY(settle(repo()));
        repo().deleteBranch(u"done"_s, false);
        QVERIFY(settle(repo()));
        QCOMPARE(m_errors->size(), 1);
        m_errors->clear();
        QVERIFY(repo().branches().contains(u"done"_s));
        repo().deleteBranch(u"done"_s, true);
        QVERIFY(settle(repo()));
        QVERIFY(!repo().branches().contains(u"done"_s));
        repo().deleteBranch(u"old"_s, false);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().branches(), QStringList{u"main"_s});
    }

    // A remote-tracking branch checked out makes a local one tracking it.
    void checksOutRemoteBranches()
    {
        QTemporaryDir remote;
        GitRunner(remote.path()).run({u"init"_s, u"-q"_s, u"--bare"_s, remote.path()});
        git({"remote", "add", "origin", remote.path()});
        git({"push", "-q", "origin", "main", "main:topic"});
        git({"fetch", "-q", "origin"});
        repo().setShowRemotes(true);
        open(m_dir->path());
        QVERIFY(repo().remoteBranches().contains(u"origin/topic"_s));
        repo().checkoutBranch(u"origin/topic"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().branch(), u"topic"_s);
        QCOMPARE(git({"rev-parse", "--abbrev-ref", "topic@{upstream}"}).trimmed(), "origin/topic"_ba);
    }

    // Pulling brings in what the upstream has, merged or rebased as
    // pull.rebase says.
    void pullsTheUpstream()
    {
        QTemporaryDir remote;
        GitRunner(remote.path()).run({u"init"_s, u"-q"_s, u"--bare"_s, u"-b"_s, u"main"_s, remote.path()});
        git({"remote", "add", "origin", remote.path()});
        git({"push", "-q", "-u", "origin", "main"});
        // Someone else's commit on the remote
        QTemporaryDir other;
        GitRunner(other.path()).run({u"clone"_s, u"-q"_s, remote.path(), other.path()});
        GitRunner them(other.path());
        writeFile(other.filePath(u"theirs.txt"_s), "theirs\n");
        them.run({u"add"_s, u"theirs.txt"_s});
        them.run({u"commit"_s, u"-q"_s, u"-m"_s, u"Theirs"_s});
        them.run({u"push"_s, u"-q"_s});

        open(m_dir->path());
        repo().pull();
        QVERIFY(settle(repo()));
        QVERIFY(QFile::exists(m_dir->filePath(u"theirs.txt"_s)));
        QCOMPARE(repo().behind(), 0);

        // A commit of ours and another of theirs: rebased onto theirs
        writeFile(m_dir->filePath(u"ours.txt"_s), "ours\n");
        git({"add", "ours.txt"});
        git({"commit", "-q", "-m", "Ours"});
        writeFile(other.filePath(u"later.txt"_s), "later\n");
        them.run({u"pull"_s, u"-q"_s});
        them.run({u"add"_s, u"later.txt"_s});
        them.run({u"commit"_s, u"-q"_s, u"-m"_s, u"Later"_s});
        them.run({u"push"_s, u"-q"_s});
        git({"config", "branch.main.rebase", "true"}); // as git pull: the branch's setting counts
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().pull();
        QVERIFY(settle(repo()));
        QCOMPARE(git({"log", "-3", "--format=%s"}), "Ours\nLater\nTheirs\n"_ba); // no merge commit
        QVERIFY(m_errors->isEmpty());

        // Diverged again, with a change not committed: only a fast-forward
        // is refused; merging, as asked over the branch's setting, puts the
        // change aside meanwhile
        writeFile(m_dir->filePath(u"ours.txt"_s), "ours, again\n");
        git({"commit", "-q", "-am", "Ours again"});
        writeFile(m_dir->filePath(u"ours.txt"_s), "ours, not committed\n");
        them.run({u"pull"_s, u"-q"_s});
        writeFile(other.filePath(u"third.txt"_s), "third\n");
        them.run({u"add"_s, u"third.txt"_s});
        them.run({u"commit"_s, u"-q"_s, u"-m"_s, u"Third"_s});
        them.run({u"push"_s, u"-q"_s});
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().pull(u"ff-only"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(m_errors->size(), 1);
        m_errors->clear();
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Ours again\n"_ba);
        repo().pull(u"merge"_s, true);
        QVERIFY(settle(repo()));
        QVERIFY2(m_errors->isEmpty(), qPrintable(m_errors->value(0).value(0).toString()));
        QCOMPARE(git({"log", "-1", "--format=%P"}).trimmed().split(' ').size(), 2); // merged
        QVERIFY(QFile::exists(m_dir->filePath(u"third.txt"_s)));
        QCOMPARE(readFile(m_dir->filePath(u"ours.txt"_s)), "ours, not committed\n"_ba);
    }

    // A branch merged with a merge commit always, only fast-forwarding,
    // or squashed into staged changes
    void mergesAsAsked()
    {
        git({"checkout", "-q", "-b", "feature"});
        writeFile(m_dir->filePath(u"feature.txt"_s), "feature\n");
        git({"add", "feature.txt"});
        git({"commit", "-q", "-m", "Feature"});
        git({"checkout", "-q", "main"});
        open(m_dir->path());
        const QString start = QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed());

        repo().mergeBranch(u"feature"_s, u"no-ff"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(git({"log", "-1", "--format=%P"}).trimmed().split(' ').size(), 2);

        git({"reset", "-q", "--hard", start});
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().mergeBranch(u"feature"_s, u"squash"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed()), start);
        QVERIFY(git({"diff", "--cached", "--name-only"}).contains("feature.txt"));
        QCOMPARE(repo().operation(), QString()); // nothing to continue, only to commit
        QVERIFY(m_errors->isEmpty());

        git({"reset", "-q", "--hard", start});
        writeFile(m_dir->filePath(u"main.txt"_s), "main\n");
        git({"add", "main.txt"});
        git({"commit", "-q", "-m", "Main"});
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().mergeBranch(u"feature"_s, u"ff-only"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(m_errors->size(), 1);
        m_errors->clear();
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Main\n"_ba);
    }

    // A merge that stops at a conflict waits for it; resolved and staged,
    // it goes on to its commit. A rebase stopped so is aborted, and
    // everything is as before.
    void continuesAndAbortsOperations()
    {
        git({"checkout", "-q", "-b", "other"});
        writeFile(file(), "one\nTWO there\nthree\nfour\nfive\n");
        git({"commit", "-q", "-am", "There"});
        git({"checkout", "-q", "main"});
        writeFile(file(), "one\nTWO here\nthree\nfour\nfive\n");
        git({"commit", "-q", "-am", "Here"});
        open(m_dir->path());
        const QByteArray before = git({"rev-parse", "HEAD"});

        repo().mergeBranch(u"other"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().operation(), u"Merging"_s);
        QCOMPARE(m_errors->size(), 1);
        QVERIFY(m_errors->takeFirst().first().toString().contains(u"conflicts"_s));
        writeFile(file(), "one\nTWO both\nthree\nfour\nfive\n");
        git({"add", "f.txt"});
        repo().continueOperation();
        QVERIFY(settle(repo()));
        QCOMPARE(repo().operation(), QString());
        QCOMPARE(git({"log", "-1", "--format=%P"}).trimmed().split(' ').size(), 2); // a merge commit
        QVERIFY(m_errors->isEmpty());

        git({"reset", "-q", "--hard", QString::fromUtf8(before.trimmed())});
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().rebaseOnto(u"other"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().operation(), u"Rebasing"_s);
        m_errors->clear();
        repo().abortOperation();
        QVERIFY(settle(repo()));
        QCOMPARE(repo().operation(), QString());
        QCOMPARE(git({"rev-parse", "HEAD"}), before);
        QCOMPARE(repo().branch(), u"main"_s);
        QVERIFY(m_errors->isEmpty());
    }

    // Changes thrown away: a file's unstaged ones, its staged ones staying;
    // an untracked file; some lines of a file, the others staying.
    void discardsChanges()
    {
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        git({"add", "f.txt"});
        writeFile(file(), "one\nTWO\nthree\nFOUR\nfive\n");
        writeFile(m_dir->filePath(u"new.txt"_s), "new\n");
        open(m_dir->path());
        repo().discardFile(u"f.txt"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(readFile(file()), "one\nTWO\nthree\nfour\nfive\n"_ba); // the staged change kept
        repo().discardFile(u"new.txt"_s);
        QVERIFY(settle(repo()));
        QVERIFY(!QFile::exists(m_dir->filePath(u"new.txt"_s)));

        // Two lines changed, one of them thrown away
        writeFile(file(), "ONE\nTWO\nthree\nfour\nFIVE\n");
        repo().refresh();
        QVERIFY(settle(repo()));
        showFile(u"f.txt"_s, false);
        selectLinesContaining({u"five"_s, u"FIVE"_s});
        repo().discardSelection();
        QVERIFY(settle(repo()));
        QCOMPARE(readFile(file()), "ONE\nTWO\nthree\nfour\nfive\n"_ba);

        // Some lines of an untracked file, the others staying
        writeFile(m_dir->filePath(u"notes.txt"_s), "keep\ndrop\nkeep too\n");
        repo().refresh();
        QVERIFY(settle(repo()));
        showFile(u"notes.txt"_s, false);
        selectLinesContaining({u"drop"_s});
        repo().discardSelection();
        QVERIFY(settle(repo()));
        QCOMPARE(readFile(m_dir->filePath(u"notes.txt"_s)), "keep\nkeep too\n"_ba);
        QVERIFY(m_errors->isEmpty());
    }

    // Stashes made, listed, applied, popped and dropped.
    void stashesChanges()
    {
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        writeFile(m_dir->filePath(u"new.txt"_s), "new\n");
        open(m_dir->path());
        repo().stash(u"Half a thought"_s, true);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().unstaged()->count(), 0);
        QVERIFY(!QFile::exists(m_dir->filePath(u"new.txt"_s)));
        QCOMPARE(repo().stashes().size(), 1);
        QVERIFY(repo().stashes().first().contains(u"Half a thought"_s));

        writeFile(m_dir->filePath(u"g.txt"_s), "g again\n");
        repo().stash({}, false);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().stashes().size(), 2); // the newest first
        QVERIFY(repo().stashes().last().contains(u"Half a thought"_s));

        repo().applyStash(1, false);  // kept
        QVERIFY(settle(repo()));
        QCOMPARE(readFile(file()), "one\nTWO\nthree\nfour\nfive\n"_ba);
        QVERIFY(QFile::exists(m_dir->filePath(u"new.txt"_s)));
        QCOMPARE(repo().stashes().size(), 2);
        repo().dropStash(1);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().stashes().size(), 1);
        repo().applyStash(0, true); // popped
        QVERIFY(settle(repo()));
        QCOMPARE(readFile(m_dir->filePath(u"g.txt"_s)), "g again\n"_ba);
        QVERIFY(repo().stashes().isEmpty());
        QVERIFY(m_errors->isEmpty());
    }

    // A commit picked from another branch, one undone by a new one, and the
    // branch reset to an earlier commit in each of git's modes.
    void picksRevertsAndResets()
    {
        git({"checkout", "-q", "-b", "other"});
        writeFile(m_dir->filePath(u"picked.txt"_s), "picked\n");
        git({"add", "picked.txt"});
        git({"commit", "-q", "-m", "Picked"});
        const QString picked = QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed());
        git({"checkout", "-q", "main"});
        open(m_dir->path());

        repo().cherryPick(picked);
        QVERIFY(settle(repo()));
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Picked\n"_ba);
        QVERIFY(QFile::exists(m_dir->filePath(u"picked.txt"_s)));
        // Picked again, from other, whose change main has now: refused,
        // nothing left open; and one that is main's own
        repo().cherryPick(picked);
        QVERIFY(settle(repo()));
        QCOMPARE(m_errors->size(), 1);
        QCOMPARE(repo().operation(), QString());
        repo().cherryPick(QString::fromUtf8(git({"rev-parse", "HEAD~1"}).trimmed()));
        QVERIFY(settle(repo()));
        QCOMPARE(m_errors->size(), 2);
        QCOMPARE(repo().operation(), QString());
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Picked\n"_ba);
        m_errors->clear();

        repo().revertCommit(QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed()));
        QVERIFY(settle(repo()));
        QVERIFY(git({"log", "-1", "--format=%s"}).startsWith("Revert \"Picked\""));
        QVERIFY(!QFile::exists(m_dir->filePath(u"picked.txt"_s)));

        // Back to Second: soft keeps what lies between staged
        const QString second = QString::fromUtf8(git({"rev-parse", "HEAD~2"}).trimmed());
        const QString tip = QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed());
        repo().resetTo(second, u"soft"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().headSha(), second);
        QCOMPARE(git({"diff", "--cached", "--name-only"}), QByteArray()); // picked and reverted: nothing in all
        git({"reset", "-q", "--hard", tip});

        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().resetTo(QString::fromUtf8(git({"rev-parse", "HEAD~3"}).trimmed()), u"mixed"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().staged()->count(), 0);
        QVERIFY(repo().unstaged()->count() >= 2); // g.txt back as untracked, f.txt changed

        repo().resetTo(tip, u"hard"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().headSha(), tip);
        QCOMPARE(readFile(file()), "one\ntwo\nthree\nfour\nfive\n"_ba);
        QVERIFY(m_errors->isEmpty());
    }

    // Two commits compared, and what a branch changed since it split off,
    // without what happened on the other side meanwhile.
    void comparesCommits()
    {
        const QString first = QString::fromUtf8(git({"rev-parse", "HEAD~1"}).trimmed());
        git({"checkout", "-q", "-b", "other"});
        writeFile(m_dir->filePath(u"theirs.txt"_s), "theirs\n");
        git({"add", "theirs.txt"});
        git({"commit", "-q", "-m", "Theirs"});
        git({"checkout", "-q", "main"});
        writeFile(m_dir->filePath(u"ours.txt"_s), "ours\n");
        git({"add", "ours.txt"});
        git({"commit", "-q", "-m", "Ours"});
        open(m_dir->path());

        repo().compareCommits(first, u"HEAD"_s, u"First"_s, false);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().diffSource(), Repository::CompareDiff);
        QCOMPARE(repo().diffBaseName(), u"First"_s);
        QCOMPARE(repo().diff()->fileCount(), 2); // g.txt and ours.txt

        repo().compareCommits(u"HEAD"_s, u"other"_s, u"main"_s, true);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().diff()->fileCount(), 1); // theirs.txt only, not ours.txt
        QVERIFY(repo().commitDetails().contains(u"since it split off"_s));
        QVERIFY(m_errors->isEmpty());
    }

    // Tags made, light and annotated, pushed and deleted.
    void tagsCommits()
    {
        QTemporaryDir remote;
        GitRunner(remote.path()).run({u"init"_s, u"-q"_s, u"--bare"_s, u"-b"_s, u"main"_s, remote.path()});
        git({"remote", "add", "origin", remote.path()});
        git({"push", "-q", "-u", "origin", "main"});
        open(m_dir->path());
        QVERIFY(repo().tagNameError(u"v1.0"_s).isEmpty());
        QVERIFY(!repo().tagNameError(u"v 1"_s).isEmpty());

        repo().createTag(u"v1.0"_s, QString::fromUtf8(git({"rev-parse", "HEAD~1"}).trimmed()), {});
        QVERIFY(settle(repo()));
        repo().createTag(u"v1.1"_s, {}, u"The second"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(git({"cat-file", "-t", "v1.0"}).trimmed(), "commit"_ba); // light
        QCOMPARE(git({"cat-file", "-t", "v1.1"}).trimmed(), "tag"_ba);    // annotated
        QVERIFY(repo().tags().contains(u"v1.1"_s));
        QVERIFY(!repo().tagNameError(u"v1.1"_s).isEmpty()); // taken
        repo().setShowTags(false);
        QVERIFY(settle(repo()));
        QVERIFY(!repo().tagNameError(u"v1.1"_s).isEmpty()); // also when tags are not shown
        repo().setShowTags(true);
        QVERIFY(settle(repo()));

        repo().pushTag(u"v1.1"_s);
        QVERIFY(settle(repo()));
        QVERIFY(GitRunner(remote.path()).run({u"rev-parse"_s, u"-q"_s, u"--verify"_s, u"refs/tags/v1.1"_s}).ok());

        repo().deleteTag(u"v1.0"_s);
        QVERIFY(settle(repo()));
        QVERIFY(!repo().tags().contains(u"v1.0"_s));
        QVERIFY(m_errors->isEmpty());
    }

    // A repository cloned, and a new one made; each opened once there.
    void clonesAndInits()
    {
        QTemporaryDir remote;
        GitRunner(remote.path()).run({u"clone"_s, u"-q"_s, u"--bare"_s, m_dir->path(), remote.filePath(u"widget.git"_s)});
        QTemporaryDir parent;
        QCOMPARE(repo().cloneFolderName(remote.filePath(u"widget.git"_s)), u"widget"_s);
        QCOMPARE(repo().cloneFolderName(u"git@github.com:acme/widget.git"_s), u"widget"_s);
        QCOMPARE(repo().cloneFolderName(u"https://example.org/x/tool/"_s), u"tool"_s);
        QCOMPARE(repo().cloneFolderName(u"C:\\repos\\widget.git"_s), u"widget"_s);
        repo().cloneRepository(remote.filePath(u"widget.git"_s), parent.path());
        QVERIFY(settle(repo()));
        QCOMPARE(repo().path(), QDir(parent.filePath(u"widget"_s)).canonicalPath());
        QCOMPARE(repo().commits()->rowCount(), 2);
        QCOMPARE(repo().remotes().size(), 1);

        repo().initRepository(parent.filePath(u"fresh"_s));
        QVERIFY(settle(repo()));
        QCOMPARE(repo().path(), QDir(parent.filePath(u"fresh"_s)).canonicalPath());
        QVERIFY(repo().valid());
        QCOMPARE(repo().commits()->rowCount(), 0);
        QVERIFY(m_errors->isEmpty());
    }

    // Remotes added, renamed, pointed elsewhere and removed, and one's
    // branch made the upstream.
    void managesRemotes()
    {
        QTemporaryDir place;
        // A path with a space, which git remote -v prints as it is
        const QString remotePath = place.filePath(u"my remote.git"_s);
        GitRunner(place.path()).run({u"init"_s, u"-q"_s, u"--bare"_s, u"-b"_s, u"main"_s, remotePath});
        open(m_dir->path());
        repo().addRemote(u"origin"_s, remotePath);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().remotes().size(), 1);
        QCOMPARE(repo().remotes().first().toMap().value(u"url"_s).toString(), remotePath);
        git({"push", "-q", "origin", "main"});
        repo().renameRemote(u"origin"_s, u"upstream"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().remotes().first().toMap().value(u"name"_s).toString(), u"upstream"_s);
        repo().setUpstream(u"upstream/main"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().upstream(), u"upstream/main"_s);
        repo().setRemoteUrl(u"upstream"_s, u"/elsewhere"_s);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().remotes().first().toMap().value(u"url"_s).toString(), u"/elsewhere"_s);
        repo().removeRemote(u"upstream"_s);
        QVERIFY(settle(repo()));
        QVERIFY(repo().remotes().isEmpty());
        QVERIFY(m_errors->isEmpty());
    }

    // A worktree added on a new branch, listed, and removed; submodules
    // brought to their commits.
    void managesWorktreesAndSubmodules()
    {
        QTemporaryDir place;
        open(m_dir->path());
        const QString tree = place.filePath(u"side"_s);
        repo().addWorktree(tree, u"side"_s, true, {});
        QVERIFY(settle(repo()));
        QVERIFY(QFile::exists(QDir(tree).filePath(u"f.txt"_s)));
        QCOMPARE(repo().worktrees().size(), 2);
        bool current = false;
        for (const QVariant &w : repo().worktrees())
            current = current || w.toMap().value(u"current"_s).toBool();
        QVERIFY(current);
        repo().removeWorktree(tree);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().worktrees().size(), 1);

        // A submodule from a repository next to it
        QTemporaryDir lib;
        GitRunner libGit(lib.path());
        libGit.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        writeFile(lib.filePath(u"lib.txt"_s), "lib\n");
        libGit.run({u"add"_s, u"lib.txt"_s});
        libGit.run({u"commit"_s, u"-q"_s, u"-m"_s, u"Lib"_s});
        git({"config", "protocol.file.allow", "always"});
        git({"-c", "protocol.file.allow=always", "submodule", "add", "-q", lib.path(), "lib"});
        git({"commit", "-q", "-m", "Add lib"});
        QDir(m_dir->filePath(u"lib"_s)).removeRecursively();
        QDir().mkpath(m_dir->filePath(u"lib"_s));
        repo().refresh();
        QVERIFY(settle(repo()));
        QVERIFY(repo().hasSubmodules());
        QVERIFY(!QFile::exists(m_dir->filePath(u"lib/lib.txt"_s)));
        repo().updateSubmodules();
        QVERIFY(settle(repo()));
        QVERIFY(QFile::exists(m_dir->filePath(u"lib/lib.txt"_s)));
        QVERIFY(m_errors->isEmpty());
    }

    // A commit saved as a patch and applied elsewhere as a commit; a plain
    // diff applied to the working tree.
    void savesAndAppliesPatches()
    {
        open(m_dir->path());
        QTemporaryDir out;
        const QString patch = out.filePath(u"second.patch"_s);
        repo().savePatch(QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed()), patch);
        QVERIFY(settle(repo()));
        QVERIFY(QFile(patch).size() > 0);

        git({"reset", "-q", "--hard", "HEAD~1"});
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().applyPatch(patch);
        QVERIFY(settle(repo()));
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Second\n"_ba);

        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        const QByteArray diff = git({"diff"});
        git({"checkout", "--", "f.txt"});
        writeFile(out.filePath(u"plain.diff"_s), diff);
        repo().applyPatch(out.filePath(u"plain.diff"_s));
        QVERIFY(settle(repo()));
        QCOMPARE(readFile(file()), "one\nTWO\nthree\nfour\nfive\n"_ba);
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Second\n"_ba); // no commit
        QVERIFY(m_errors->isEmpty());

        // An am stopped in a terminal stays as it is
        git({"checkout", "--", "f.txt"});
        writeFile(file(), "one\ntwo\nTHREE\nfour\nfive\n");
        git({"commit", "-qam", "Three"});
        const QByteArray three = git({"format-patch", "-1", "--stdout", "HEAD"});
        git({"reset", "-q", "--hard", "HEAD~1"});
        writeFile(file(), "one\ntwo\nthree, other\nfour\nfive\n");
        git({"commit", "-qam", "Other three"});
        writeFile(out.filePath(u"three.patch"_s), three);
        QVERIFY(!m_git.run({u"am"_s, out.filePath(u"three.patch"_s)}).ok()); // stopped
        const QByteArray head = git({"rev-parse", "HEAD"});
        repo().refresh();
        QVERIFY(settle(repo()));
        repo().applyPatch(patch);
        QVERIFY(settle(repo()));
        QCOMPARE(m_errors->size(), 1);
        QVERIFY(m_errors->takeFirst().first().toString().contains(u"am is going on"_s));
        QVERIFY(QDir(m_dir->filePath(u".git/rebase-apply"_s)).exists());
        QCOMPARE(git({"rev-parse", "HEAD"}), head);
        git({"am", "--abort"});
    }

    // A bisection from a good and a bad commit to the first bad one, and
    // back where it began.
    // Work no branch holds any more: a branch deleted after it was
    // checked out, one never checked out, a dropped stash
    void findsLostWork()
    {
        git({"checkout", "-q", "-b", "side"});
        writeFile(m_dir->filePath(u"side.txt"_s), "side\n");
        git({"add", "side.txt"});
        git({"commit", "-q", "-m", "Work on the side"});
        const QString side = QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed());
        git({"checkout", "-q", "main"});
        git({"branch", "-q", "-D", "side"});

        const QString tree = QString::fromUtf8(git({"rev-parse", "HEAD^{tree}"}).trimmed());
        const QString never = QString::fromUtf8(
            gitWithInput({"commit-tree", tree, "-p", "HEAD"}, "Never checked out\n").trimmed());
        git({"branch", "-q", "never", never});
        git({"branch", "-q", "-D", "never"});

        writeFile(m_dir->filePath(u"g.txt"_s), "stashed\n");
        git({"stash", "push", "-q", "-m", "half done"});
        const QString stash = QString::fromUtf8(git({"rev-parse", "stash@{0}"}).trimmed());
        git({"stash", "drop", "-q"});

        open(m_dir->path());
        QSignalSpy found(m_repo.get(), &Repository::lostWorkFound);
        repo().findLostWork();
        QVERIFY(settle(repo()));
        QCOMPARE(found.size(), 1);
        const QVariantList commits = found.first().first().toList();
        const auto entry = [&](const QString &sha, const QString &kind) {
            for (const QVariant &commit : commits) {
                const QVariantMap map = commit.toMap();
                if (map.value(u"sha"_s) == sha && map.value(u"kind"_s) == kind)
                    return map;
            }
            return QVariantMap();
        };
        const QVariantMap onSide = entry(side, u"reflog"_s);
        QVERIFY(!onSide.isEmpty());
        QVERIFY(onSide.value(u"unreachable"_s).toBool());
        QCOMPARE(onSide.value(u"subject"_s).toString(), u"Work on the side"_s);
        QVERIFY(onSide.value(u"selector"_s).toString().startsWith(u"HEAD@{"_s));
        QVERIFY(onSide.value(u"action"_s).toString().startsWith(u"commit"_s));
        // The commit main is on, still had
        const QString head = QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed());
        QVERIFY(!entry(head, u"reflog"_s).value(u"unreachable"_s).toBool());
        const QVariantMap lost = entry(never, u"dangling"_s);
        QVERIFY(lost.value(u"unreachable"_s).toBool());
        QVERIFY(!lost.value(u"stash"_s).toBool());
        const QVariantMap dropped = entry(stash, u"dangling"_s);
        QVERIFY(dropped.value(u"stash"_s).toBool());

        repo().restoreStash(stash, dropped.value(u"subject"_s).toString());
        QVERIFY(settle(repo()));
        QCOMPARE(QString::fromUtf8(git({"rev-parse", "stash@{0}"}).trimmed()), stash);
        QVERIFY(git({"stash", "list"}).contains("half done"));
    }

    void bisects()
    {
        for (int i = 3; i <= 8; ++i) {
            writeFile(m_dir->filePath(u"n.txt"_s), QByteArray::number(i) + '\n');
            git({"add", "n.txt"});
            git({"commit", "-q", "-m", QStringLiteral("Step %1").arg(i)});
        }
        open(m_dir->path());
        const QString good = QString::fromUtf8(git({"rev-parse", "HEAD~6"}).trimmed());
        QVERIFY(!repo().bisecting());
        repo().bisectMark(QString::fromUtf8(git({"rev-parse", "HEAD"}).trimmed()), u"bad"_s);
        QVERIFY(settle(repo()));
        QVERIFY(repo().bisecting());
        repo().bisectMark(good, u"good"_s);
        QVERIFY(settle(repo()));
        // "Broken" from Step 6 on: answer until git names it
        QSignalSpy notices(m_repo.get(), &Repository::notice);
        for (int round = 0; round < 6; ++round) {
            const int n = readFile(m_dir->filePath(u"n.txt"_s)).trimmed().toInt();
            repo().bisectCurrent(n >= 6 ? u"bad"_s : u"good"_s);
            QVERIFY(settle(repo()));
            if (!notices.isEmpty() && notices.last().first().toString().contains(u" is the first "_s))
                break;
        }
        QVERIFY(!notices.isEmpty());
        QVERIFY(notices.last().first().toString().contains(u" is the first "_s));
        QCOMPARE(git({"log", "-1", "--format=%s", "refs/bisect/bad"}), "Step 6\n"_ba);
        repo().bisectReset();
        QVERIFY(settle(repo()));
        QVERIFY(!repo().bisecting());
        QCOMPARE(repo().branch(), u"main"_s);
        QVERIFY(m_errors->isEmpty());
    }

    // The history keeps the commits it read while nothing they depend on
    // moved, and reads them again when something did.
    void keepsTheHistoryUntilItMoves()
    {
        git({"checkout", "-q", "-b", "side"});
        writeFile(m_dir->filePath(u"side.txt"_s), "side\n");
        git({"add", "side.txt"});
        git({"commit", "-q", "-m", "Side"});
        git({"checkout", "-q", "main"});
        open(m_dir->path());
        QCOMPARE(repo().commits()->rowCount(), 3);

        // A tag at a commit read: shown, the commits as they were
        repo().createTag(u"v1"_s, QString::fromUtf8(git({"rev-parse", "HEAD~1"}).trimmed()), {});
        QVERIFY(settle(repo()));
        QCOMPARE(repo().commits()->rowCount(), 3);
        const int row = repo().commits()->rowOf(QString::fromUtf8(git({"rev-parse", "HEAD~1"}).trimmed()));
        bool tagged = false;
        for (const QVariant &label : repo().commits()->data(repo().commits()->index(row), CommitModel::RefsRole).toList())
            tagged = tagged || label.toMap().value(u"name"_s).toString() == u"v1"_s;
        QVERIFY(tagged);

        // The branch deleted: its commit gone with it
        repo().deleteBranch(u"side"_s, true);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().commits()->rowCount(), 2);

        // A commit made elsewhere: read
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        git({"commit", "-qam", "Third"});
        repo().refresh();
        QVERIFY(settle(repo()));
        QCOMPARE(repo().commits()->rowCount(), 3);

        // Only the current branch: read again, as the filter changed
        git({"branch", "other", "HEAD~1"});
        git({"checkout", "-q", "other"});
        writeFile(m_dir->filePath(u"other.txt"_s), "other\n");
        git({"add", "other.txt"});
        git({"commit", "-q", "-m", "Other"});
        repo().refresh();
        QVERIFY(settle(repo()));
        QCOMPARE(repo().commits()->rowCount(), 4);
        repo().setCurrentBranchOnly(true);
        QVERIFY(settle(repo()));
        QCOMPARE(repo().commits()->rowCount(), 3);
        QVERIFY(m_errors->isEmpty());
    }

    // How long reading a big repository takes: opening it, reading it all
    // again, its status, and its commits one after the other. Only reads.
    void benchReading()
    {
        const QString path = qEnvironmentVariable("IGITT_BENCH_REPO");
        if (qEnvironmentVariableIsEmpty("IGITT_BENCH") || path.isEmpty())
            QSKIP("set IGITT_BENCH and IGITT_BENCH_REPO to run");
        QElapsedTimer timer;
        timer.start();
        repo().open(path);
        QVERIFY(settle(repo()));
        qInfo() << "open" << timer.restart() << "ms," << repo().commits()->rowCount() << "commits";
        for (int i = 0; i < 5; ++i) {
            repo().refresh();
            QVERIFY(settle(repo()));
        }
        qInfo() << "refresh" << timer.restart() / 5 << "ms each";
        for (int i = 0; i < 10; ++i) {
            repo().refreshStatus();
            QVERIFY(settle(repo()));
        }
        qInfo() << "status" << timer.restart() / 10 << "ms each";
        for (int row = 0; row < 20; ++row) {
            repo().showCommit(repo().commits()->shaAt(row));
            QVERIFY(settle(repo()));
        }
        qInfo() << "commit diff" << timer.restart() / 20 << "ms each";
        m_errors->clear();
    }

private:
    Repository &repo() { return *m_repo; }
    QString file() const { return m_dir->filePath("f.txt"); }

    void open(const QString &path)
    {
        repo().open(path);
        QVERIFY(settle(repo()));
        QVERIFY(repo().valid());
    }

    void showFile(const QString &path, bool staged)
    {
        repo().showFile(path, staged);
        QVERIFY(settle(repo()));
    }

    QByteArray git(const QStringList &args)
    {
        const GitResult result = m_git.run(args);
        if (!result.ok())
            qWarning() << args << result.errorText();
        return result.out;
    }
    QByteArray gitWithInput(const QStringList &args, const QByteArray &input)
    {
        const GitResult result = m_git.run(args, input);
        if (!result.ok())
            qWarning() << args << result.errorText();
        return result.out;
    }

    // Row of the diff line with exactly this text, or -1.
    int rowOf(const QString &text)
    {
        DiffModel *diff = repo().diff();
        for (int row = 0; row < diff->rowCount(); ++row) {
            if (diff->data(diff->index(row), DiffModel::TextRole).toString() == text)
                return row;
        }
        return -1;
    }

    bool isChange(const QString &text)
    {
        const int row = rowOf(text);
        return row >= 0 && repo().diff()->data(repo().diff()->index(row), DiffModel::SelectableRole).toBool();
    }

    // Selects every changed line whose text is one of `texts`.
    void selectLinesContaining(const QStringList &texts)
    {
        DiffModel *diff = repo().diff();
        for (int row = 0; row < diff->rowCount(); ++row) {
            const QModelIndex idx = diff->index(row);
            if (diff->data(idx, DiffModel::SelectableRole).toBool()
                && texts.contains(diff->data(idx, DiffModel::TextRole).toString()))
                diff->toggleLine(row);
        }
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    GitRunner m_git;
    std::unique_ptr<Repository> m_repo;
    std::unique_ptr<QSignalSpy> m_errors;
};

QTEST_GUILESS_MAIN(TestRepository)
#include "tst_repository.moc"
