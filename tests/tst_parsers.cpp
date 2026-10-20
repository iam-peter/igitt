#include "core/DiffParser.h"
#include "core/LogParser.h"
#include "core/StatusParser.h"

#include <QTest>

using namespace Qt::StringLiterals;

class TestParsers : public QObject
{
    Q_OBJECT

private slots:
    void parseLogRecords()
    {
        const QByteArray out =
            "1111\x1f" "2222 3333\x1f" "Jane Doe\x1f" "jane@example.com\x1f" "1700000000\x1f" "Merge things\0"
            "\n2222\x1f\x1f" "John\x1f" "john@example.com\x1f" "1600000000\x1f" "Initial\0"_ba;
        const QList<CommitInfo> commits = parseLog(out);
        QCOMPARE(commits.size(), 2);
        QCOMPARE(commits[0].sha, u"1111"_s);
        QCOMPARE(commits[0].parents, (QStringList{u"2222"_s, u"3333"_s}));
        QCOMPARE(commits[0].authorName, u"Jane Doe"_s);
        QCOMPARE(commits[0].subject, u"Merge things"_s);
        QVERIFY(commits[1].parents.isEmpty());
    }

    void parseRefsPeelsTagsAndSkipsRemoteHead()
    {
        const QByteArray out =
            "aaaa\t\trefs/heads/main\n"
            "bbbb\t\trefs/remotes/origin/HEAD\n"
            "bbbb\t\trefs/remotes/origin/main\n"
            "tagobj\tcccc\trefs/tags/v1.0\n"_ba;
        const QList<RefInfo> refs = parseRefs(out);
        QCOMPARE(refs.size(), 3);
        QCOMPARE(refs[0].name, u"main"_s);
        QCOMPARE(refs[0].kind, RefInfo::LocalBranch);
        QCOMPARE(refs[1].name, u"origin/main"_s);
        QCOMPARE(refs[2].sha, u"cccc"_s);
        QCOMPARE(refs[2].kind, RefInfo::Tag);
    }

    void parseStatusEntries()
    {
        const QByteArray out =
            "# branch.oid 0123456789abcdef\0"
            "# branch.head main\0"
            "# branch.upstream origin/main\0"
            "# branch.ab +2 -1\0"
            "1 .M N... 100644 100644 100644 aaaa aaaa src/with space.cpp\0"
            "1 A. N... 000000 100644 100644 0000 bbbb new.txt\0"
            "2 R. N... 100644 100644 100644 cccc cccc R100 renamed.txt\0old.txt\0"
            "u UU N... 100644 100644 100644 100644 d1 d2 d3 conflict.txt\0"
            "? untracked.txt\0"_ba;
        const StatusInfo info = parseStatus(out);
        QCOMPARE(info.branch, u"main"_s);
        QCOMPARE(info.upstream, u"origin/main"_s);
        QCOMPARE(info.ahead, 2);
        QCOMPARE(info.behind, 1);
        QCOMPARE(info.entries.size(), 5);

        QCOMPARE(info.entries[0].path, u"src/with space.cpp"_s);
        QVERIFY(info.entries[0].hasUnstagedChange());
        QVERIFY(!info.entries[0].hasStagedChange());
        QVERIFY(info.entries[1].hasStagedChange());
        QCOMPARE(info.entries[2].path, u"renamed.txt"_s);
        QCOMPARE(info.entries[2].origPath, u"old.txt"_s);
        QVERIFY(info.entries[3].conflicted);
        QVERIFY(info.entries[4].untracked);
    }

    void parseUnbornStatus()
    {
        const StatusInfo info = parseStatus("# branch.oid (initial)\0# branch.head main\0"_ba);
        QVERIFY(info.headSha.isEmpty());
        QCOMPARE(info.branch, u"main"_s);
    }

    void parseDiffFiles()
    {
        const QByteArray out =
            "diff --git a/f.txt b/f.txt\n"
            "index 1111111..2222222 100644\n"
            "--- a/f.txt\n"
            "+++ b/f.txt\n"
            "@@ -1,3 +1,3 @@ section\n"
            " one\n"
            "--- not a header\n"
            "+two\n"
            " three\n"
            "diff --git a/n.txt b/n.txt\n"
            "new file mode 100644\n"
            "index 0000000..3333333\n"
            "--- /dev/null\n"
            "+++ b/n.txt\n"
            "@@ -0,0 +1 @@\n"
            "+hello\n"
            "\\ No newline at end of file\n"
            "diff --git a/img.png b/img.png\n"
            "index 4444444..5555555 100644\n"
            "Binary files a/img.png and b/img.png differ\n"_ba;
        const QList<FileDiff> files = parseDiff(out);
        QCOMPARE(files.size(), 3);

        QCOMPARE(files[0].path(), u"f.txt"_s);
        QCOMPARE(files[0].hunks.size(), 1);
        const DiffHunk &hunk = files[0].hunks[0];
        QCOMPARE(hunk.section, "section"_ba);
        QCOMPARE(hunk.lines.size(), 4);
        QCOMPARE(hunk.lines[1].kind, DiffLine::Removed);
        QCOMPARE(hunk.lines[1].content, "-- not a header"_ba);
        QCOMPARE(hunk.lines[1].oldLine, 2);
        QCOMPARE(hunk.lines[2].newLine, 2);

        QVERIFY(files[1].isNew);
        QCOMPARE(files[1].path(), u"n.txt"_s);
        QCOMPARE(files[1].hunks[0].newCount, 1);
        QCOMPARE(files[1].hunks[0].lines.last().kind, DiffLine::NoNewline);

        QVERIFY(files[2].isBinary);
        QCOMPARE(files[2].path(), u"img.png"_s);
    }
};

QTEST_GUILESS_MAIN(TestParsers)
#include "tst_parsers.moc"
