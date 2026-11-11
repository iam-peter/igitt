// Change clusters: hunks of several files grouped by intent, each group a
// commit of exactly its hunks, the working tree left alone.

#include "core/ChangeClusters.h"
#include "core/ChangeMove.h"
#include "core/GitRunner.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// Twenty numbered lines, some of them changed
QByteArray lines(const QHash<int, QByteArray> &changes)
{
    QByteArray text;
    for (int i = 1; i <= 20; ++i)
        text += changes.value(i, "line " + QByteArray::number(i)) + '\n';
    return text;
}

} // namespace

class TestClusters : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Ada");
        qputenv("GIT_AUTHOR_EMAIL", "ada@example.com");
        qputenv("GIT_COMMITTER_NAME", "Ada");
        qputenv("GIT_COMMITTER_EMAIL", "ada@example.com");
    }

    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_git.setWorkDir(m_dir->path());
        git({"init", "-q", "-b", "main"});
        write("ui.txt", lines({}));
        write("core.txt", lines({}));
        git({"add", "."});
        git({"commit", "-q", "-m", "Start"});

        // Two hunks in each file: a feature at the top, a fix at the bottom
        write("ui.txt", lines({{2, "dark mode toggle"}, {18, "timeout label"}}));
        write("core.txt", lines({{3, "dark mode setting"}, {17, "timeout retry"}}));
        write("notes.txt", "dark mode notes\n");
    }

    void listsEveryHunkUnassigned()
    {
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        QCOMPARE(clusters.hunks().size(), 5);
        QCOMPARE(clusters.files().size(), 3);
        QCOMPARE(clusters.unassignedCount(), 5);
        QVERIFY(clusters.error().isEmpty());
    }

    // Two clusters across both files become two commits in their order,
    // each with exactly its hunks; the working tree does not change.
    void commitsClustersAsAStack()
    {
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        const int feature = clusters.addCluster(u"Add dark mode"_s);
        const int fix = clusters.addCluster(u"Retry on timeout"_s);
        for (const QVariant &value : clusters.hunks()) {
            const QVariantMap hunk = value.toMap();
            const bool isFeature = hunkText(hunk).contains("dark mode");
            clusters.assign(hunk.value(u"id"_s).toString(), isFeature ? feature : fix);
        }
        QCOMPARE(clusters.unassignedCount(), 0);
        // The fix first
        clusters.moveCluster(fix, 0);

        const QByteArray tree = git({"status", "--porcelain"});
        QSignalSpy committed(&clusters, &ChangeClusters::clusterCommitted);
        QVERIFY2(clusters.commitAll(), qPrintable(clusters.error()));
        QCOMPARE(committed.size(), 2);

        QCOMPARE(git({"log", "--format=%s"}), "Add dark mode\nRetry on timeout\nStart\n"_ba);
        const QByteArray fixCommit = git({"show", "HEAD~1", "--format="});
        QVERIFY(fixCommit.contains("+timeout label"));
        QVERIFY(fixCommit.contains("+timeout retry"));
        QVERIFY(!fixCommit.contains("dark mode"));
        const QByteArray featureCommit = git({"show", "HEAD", "--format="});
        QVERIFY(featureCommit.contains("+dark mode toggle"));
        QVERIFY(featureCommit.contains("+dark mode setting"));
        QVERIFY(featureCommit.contains("+dark mode notes"));
        QVERIFY(!featureCommit.contains("timeout"));

        // Everything is committed, nothing is left staged or changed
        QCOMPARE(git({"status", "--porcelain"}), QByteArray());
        QVERIFY(!tree.isEmpty());
        QVERIFY(clusters.hunks().isEmpty());
        QVERIFY(clusters.clusters().isEmpty());
    }

    // Committing one cluster leaves the others' hunks where they were,
    // still assigned, although their line numbers moved.
    void keepsTheOtherClustersAcrossACommit()
    {
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        const int first = clusters.addCluster(u"Top"_s);
        const int second = clusters.addCluster(u"Bottom"_s);
        for (const QVariant &value : clusters.hunks()) {
            const QVariantMap hunk = value.toMap();
            const bool top = hunk.value(u"line"_s).toInt() < 10;
            clusters.assign(hunk.value(u"id"_s).toString(), top ? first : second);
        }

        clusters.renameCluster(first, u"Change the tops"_s);
        QVERIFY2(clusters.commitCluster(first), qPrintable(clusters.error()));
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Change the tops\n"_ba);
        // Still changed in the working tree, and no longer staged
        QCOMPARE(git({"diff", "--cached", "--name-only"}), QByteArray());
        QCOMPARE(git({"diff", "--name-only"}), "core.txt\nui.txt\n"_ba);

        QCOMPARE(clusters.clusters().size(), 1);
        QCOMPARE(clusters.hunks().size(), 2);
        for (const QVariant &value : clusters.hunks()) {
            const QVariantMap hunk = value.toMap();
            QCOMPARE(hunk.value(u"cluster"_s).toInt(), second);
        }
    }

    // What is staged outside the clusters stays staged.
    void leavesTheStagingAreaOfOtherFiles()
    {
        write("other.txt", "staged\n");
        git({"add", "other.txt"});
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        const int cluster = clusters.addCluster(u"UI"_s);
        clusters.assignFile(u"ui.txt"_s, cluster);
        QVERIFY2(clusters.commitCluster(cluster), qPrintable(clusters.error()));
        QCOMPARE(git({"log", "-1", "--format=%s"}), "UI\n"_ba);
        QCOMPARE(git({"show", "HEAD", "--format=", "--name-only"}), "ui.txt\n"_ba);
        QCOMPARE(git({"diff", "--cached", "--name-only"}), "other.txt\n"_ba);
    }

    // The lines of one hunk go to two clusters: each commit has its own
    // line, whatever the other cluster did to the hunk before.
    void splitsAHunkByLines()
    {
        // One line apart, so one hunk; lines replaced side by side would
        // leave the order of their halves open, as in git add -p
        write("ui.txt", lines({{9, "dark mode toggle"}, {11, "timeout label"}}));
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        QString hunkId;
        QVariantList rows;
        for (const QVariant &value : clusters.hunks()) {
            if (value.toMap().value(u"path"_s) == "ui.txt"_L1) {
                hunkId = value.toMap().value(u"hunk"_s).toString();
                rows = value.toMap().value(u"lines"_s).toList();
            }
        }
        QVERIFY(!hunkId.isEmpty());
        QList<int> feature;
        QList<int> fix;
        for (const QVariant &value : rows) {
            const QVariantMap line = value.toMap();
            (line.value(u"text"_s).toString().contains(u"dark"_s) || line.value(u"text"_s) == "line 9"_L1
                 ? feature : fix).append(line.value(u"index"_s).toInt());
        }
        QCOMPARE(feature.size(), 2);
        QCOMPARE(fix.size(), 2);

        const int first = clusters.addCluster(u"Add dark mode"_s);
        const int second = clusters.addCluster(u"Label the timeout"_s);
        clusters.assignLines(hunkId, feature, first);
        clusters.assignLines(hunkId, fix, second);
        // One hunk, two pieces
        const QVariantList pieces = clusters.hunks();
        QCOMPARE(std::count_if(pieces.cbegin(), pieces.cend(), [&](const QVariant &piece) {
                     return piece.toMap().value(u"hunk"_s) == hunkId && piece.toMap().value(u"split"_s).toBool();
                 }),
                 2);

        clusters.assignFile(u"core.txt"_s, second);
        clusters.removeCluster(clusters.addCluster(u"Unused"_s));
        QVERIFY2(clusters.commitAll(), qPrintable(clusters.error()));
        QCOMPARE(git({"log", "--format=%s"}), "Label the timeout\nAdd dark mode\nStart\n"_ba);
        QCOMPARE(git({"show", "HEAD~1:ui.txt"}), lines({{9, "dark mode toggle"}}));
        QCOMPARE(git({"show", "HEAD:ui.txt"}), lines({{9, "dark mode toggle"}, {11, "timeout label"}}));
    }

    // A cluster's whole message goes into its commit: title, body and
    // trailers; renaming changes only the title.
    void commitsTheWholeMessage()
    {
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        const int cluster = clusters.addCluster({});
        clusters.setClusterMessage(cluster, u"Add notes\n\nThey say what dark mode is.\n\nTask-number: QTBUG-1"_s);
        clusters.renameCluster(cluster, u"Add the notes"_s);
        QCOMPARE(clusters.clusters().first().toMap().value(u"name"_s).toString(), u"Add the notes"_s);
        clusters.assignFile(u"notes.txt"_s, cluster);
        QVERIFY2(clusters.commitCluster(cluster), qPrintable(clusters.error()));
        QCOMPARE(git({"log", "-1", "--format=%B"}).trimmed(),
                 "Add the notes\n\nThey say what dark mode is.\n\nTask-number: QTBUG-1"_ba);
    }

    // The same change twice in a file, in two clusters: committing the
    // first one's cluster leaves the second one in its own.
    void keepsARepeatedLineInItsCluster()
    {
        write("ui.txt", lines({{3, "}"}, {16, "}"}}));
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        const int first = clusters.addCluster(u"Close the top"_s);
        const int second = clusters.addCluster(u"Close the bottom"_s);
        for (const QVariant &value : clusters.hunks()) {
            const QVariantMap hunk = value.toMap();
            if (hunk.value(u"path"_s) == "ui.txt"_L1)
                clusters.assign(hunk.value(u"id"_s).toString(), hunk.value(u"line"_s).toInt() < 10 ? first : second);
        }
        QVERIFY2(clusters.commitCluster(first), qPrintable(clusters.error()));
        const QVariantList left = clusters.clusters();
        QCOMPARE(left.size(), 1);
        QCOMPARE(left.first().toMap().value(u"hunks"_s).toList().size(), 1);
        QVERIFY2(clusters.commitCluster(second), qPrintable(clusters.error()));
        QCOMPARE(git({"show", "HEAD:ui.txt"}), lines({{3, "}"}, {16, "}"}}));
    }

    // git commit would conclude a merge with the cluster alone.
    void refusesWhileAMergeIsInProgress()
    {
        const QByteArray head = git({"rev-parse", "HEAD"}).trimmed();
        QFile mergeHead(m_dir->filePath(u".git/MERGE_HEAD"_s));
        QVERIFY(mergeHead.open(QIODevice::WriteOnly));
        mergeHead.write(head + '\n');
        mergeHead.close();
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        const int cluster = clusters.addCluster(u"UI"_s);
        clusters.assignFile(u"ui.txt"_s, cluster);
        QVERIFY(!clusters.commitCluster(cluster));
        QVERIFY(clusters.error().contains(u"merge"_s));
        QCOMPARE(git({"log", "--format=%s"}), "Start\n"_ba);
    }

    // The changes against another commit than HEAD, a planned one, are
    // not committed on HEAD.
    void refusesToCommitAgainstAnotherBase()
    {
        git({"commit", "-q", "--allow-empty", "-m", "Later"});
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        clusters.setBase(u"HEAD~1"_s);
        const int cluster = clusters.addCluster(u"UI"_s);
        clusters.assignFile(u"ui.txt"_s, cluster);
        QVERIFY(!clusters.commitCluster(cluster));
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Later\n"_ba);
    }

    // The commits not merged yet come before the clusters, oldest first,
    // down to what the remote's default branch has; a change taken out of
    // one shows up in the working tree, in the cluster it was meant for.
    void readsTheCommitsNotMergedYet()
    {
        git({"add", "."});
        git({"commit", "-q", "-m", "Add dark mode"});
        write("ui.txt", lines({{2, "dark mode toggle"}, {18, "timeout label"}, {10, "spacing"}}));
        git({"commit", "-q", "-am", "Space it"});

        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        // No remote: every commit, the first one too
        QVariantList commits = clusters.commits();
        QCOMPARE(commits.size(), 3);
        QCOMPARE(commits.first().toMap().value(u"subject"_s), "Start"_L1);
        QCOMPARE(commits.last().toMap().value(u"subject"_s), "Space it"_L1);
        QVERIFY(!commits.last().toMap().value(u"published"_s).toBool());

        // Merged as far as the remote's default branch: only what is after
        git({"update-ref", "refs/remotes/origin/main", "HEAD~2"});
        git({"symbolic-ref", "refs/remotes/origin/HEAD", "refs/remotes/origin/main"});
        clusters.reload();
        commits = clusters.commits();
        QCOMPARE(commits.size(), 2);
        const QVariantMap dark = commits.first().toMap();
        QCOMPARE(dark.value(u"subject"_s), "Add dark mode"_L1);
        QCOMPARE(dark.value(u"files"_s).toStringList(), (QStringList{u"core.txt"_s, u"notes.txt"_s, u"ui.txt"_s}));

        // Its timeout label, out of the commit and into a cluster
        QString piece;
        QList<int> picked;
        for (const QVariant &value : dark.value(u"pieces"_s).toList()) {
            const QVariantMap map = value.toMap();
            if (map.value(u"path"_s) != "ui.txt"_L1 || map.value(u"line"_s).toInt() < 10)
                continue;
            piece = map.value(u"hunk"_s).toString();
            for (const QVariant &line : map.value(u"lines"_s).toList())
                picked.append(line.toMap().value(u"index"_s).toInt());
        }
        QCOMPARE(picked.size(), 2);
        const QByteArray patch = clusters.commitPatch(dark.value(u"sha"_s).toString(), piece, picked);
        QVERIFY(patch.contains("+timeout label"));
        QVERIFY(!patch.contains("+dark mode toggle"));
        const int fix = clusters.addCluster(u"Label the timeout"_s);
        clusters.expect(dark.value(u"sha"_s).toString(), piece, picked, fix);
        const FoldResult moved = moveChange(m_git, dark.value(u"sha"_s).toString(), {}, patch);
        QVERIFY2(moved.ok, qPrintable(moved.error));
        clusters.reload();

        QCOMPARE(git({"show", "HEAD~1:ui.txt"}), lines({{2, "dark mode toggle"}}));
        const QVariantList pieces = clusters.hunks();
        QCOMPARE(pieces.size(), 1);
        QCOMPARE(pieces.first().toMap().value(u"cluster"_s).toInt(), fix);
        QList<int> own;
        for (const QVariant &line : pieces.first().toMap().value(u"lines"_s).toList())
            own.append(line.toMap().value(u"index"_s).toInt());
        QVERIFY(clusters.linesPatch(pieces.first().toMap().value(u"hunk"_s).toString(), own).contains("+timeout label"));
    }

    void refusesAnEmptyCluster()
    {
        ChangeClusters clusters;
        clusters.setPath(m_dir->path());
        const int cluster = clusters.addCluster(u"Nothing"_s);
        QVERIFY(!clusters.commitCluster(cluster));
        QVERIFY(!clusters.error().isEmpty());
        QCOMPARE(git({"log", "--format=%s"}), "Start\n"_ba);
    }

private:
    static QByteArray hunkText(const QVariantMap &hunk)
    {
        QByteArray text;
        for (const QVariant &line : hunk.value(u"lines"_s).toList())
            text += line.toMap().value(u"text"_s).toString().toUtf8() + '\n';
        return text;
    }

    QByteArray git(const QStringList &args)
    {
        const GitResult result = m_git.run(args);
        if (!result.ok())
            qWarning() << args << result.err;
        return result.out;
    }

    void write(const char *name, const QByteArray &content)
    {
        QFile file(m_dir->filePath(QString::fromLatin1(name)));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    QScopedPointer<QTemporaryDir> m_dir;
    GitRunner m_git;
};

QTEST_GUILESS_MAIN(TestClusters)
#include "tst_clusters.moc"
