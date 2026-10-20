#include "core/DiffParser.h"
#include "core/PatchBuilder.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

const QByteArray Header =
    "diff --git a/f.txt b/f.txt\n"
    "index 1111111..2222222 100644\n"
    "--- a/f.txt\n"
    "+++ b/f.txt\n"_ba;

FileDiff twoChanges()
{
    return parseDiff(Header
                     + "@@ -1,5 +1,5 @@\n"
                       " one\n"
                       "-two\n"
                       "+TWO\n"
                       " three\n"
                       "-four\n"
                       "+FOUR\n"
                       " five\n"_ba)
        .first();
}

LineSelection lines(const FileDiff &file, std::initializer_list<int> selected)
{
    LineSelection selection{QList<bool>(file.hunks[0].lines.size(), false)};
    for (int l : selected)
        selection[0][l] = true;
    return selection;
}

} // namespace

class TestPatchBuilder : public QObject
{
    Q_OBJECT

private slots:
    void forwardKeepsUnselectedRemovalsAsContext()
    {
        const FileDiff file = twoChanges();
        QCOMPARE(buildPatch(file, lines(file, {1, 2}), PatchDirection::Forward),
                 Header
                     + "@@ -1,5 +1,5 @@\n"
                       " one\n"
                       "-two\n"
                       "+TWO\n"
                       " three\n"
                       " four\n"
                       " five\n"_ba);
    }

    void reverseKeepsUnselectedAdditionsAsContext()
    {
        const FileDiff file = twoChanges();
        QCOMPARE(buildPatch(file, lines(file, {4, 5}), PatchDirection::Reverse),
                 Header
                     + "@@ -1,5 +1,5 @@\n"
                       " one\n"
                       " TWO\n"
                       " three\n"
                       "-four\n"
                       "+FOUR\n"
                       " five\n"_ba);
    }

    void forwardRecountsAdditionOnly()
    {
        const FileDiff file = twoChanges();
        QCOMPARE(buildPatch(file, lines(file, {2}), PatchDirection::Forward),
                 Header
                     + "@@ -1,5 +1,6 @@\n"
                       " one\n"
                       " two\n"
                       "+TWO\n"
                       " three\n"
                       " four\n"
                       " five\n"_ba);
    }

    void laterHunksAreShiftedByEarlierOnes()
    {
        const FileDiff file = parseDiff(Header
                                        + "@@ -1,2 +1,3 @@\n"
                                          " a\n"
                                          "+b\n"
                                          " c\n"
                                          "@@ -10,2 +11,3 @@\n"
                                          " x\n"
                                          "+y\n"
                                          " z\n"_ba)
                                  .first();
        // Leaving out the first hunk moves the second one back to line 10.
        LineSelection selection{{false, false, false}, {false, true, false}};
        QCOMPARE(buildPatch(file, selection, PatchDirection::Forward),
                 Header
                     + "@@ -10,2 +10,3 @@\n"
                       " x\n"
                       "+y\n"
                       " z\n"_ba);
    }

    void emptySelectionGivesNoPatch()
    {
        const FileDiff file = twoChanges();
        QVERIFY(buildPatch(file, lines(file, {}), PatchDirection::Forward).isEmpty());
    }

    void partialNewFileBecomesModification()
    {
        const QByteArray diff =
            "diff --git a/n.txt b/n.txt\n"
            "new file mode 100644\n"
            "index 0000000..3333333\n"
            "--- /dev/null\n"
            "+++ b/n.txt\n"
            "@@ -0,0 +1,2 @@\n"
            "+a\n"
            "+b\n"_ba;
        const FileDiff file = parseDiff(diff).first();

        // Staging the whole new file keeps it a creation.
        QCOMPARE(buildPatch(file, selectAll(file), PatchDirection::Forward), diff);

        // Unstaging part of a staged new file must leave the file in place.
        QCOMPARE(buildPatch(file, lines(file, {1}), PatchDirection::Reverse),
                 "diff --git a/n.txt b/n.txt\n"
                 "index 0000000..3333333\n"
                 "--- a/n.txt\n"
                 "+++ b/n.txt\n"
                 "@@ -1,1 +1,2 @@\n"
                 " a\n"
                 "+b\n"_ba);
    }
};

QTEST_GUILESS_MAIN(TestPatchBuilder)
#include "tst_patchbuilder.moc"
