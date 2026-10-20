#include "Repository.h"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

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
