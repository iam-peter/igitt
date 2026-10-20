#include "core/GraphLayout.h"

#include <QTest>

namespace {

CommitInfo commit(const QString &sha, const QStringList &parents)
{
    CommitInfo c;
    c.sha = sha;
    c.parents = parents;
    return c;
}

} // namespace

class TestGraphLayout : public QObject
{
    Q_OBJECT

private slots:
    void linearHistoryStaysInOneLane()
    {
        const QList<GraphRow> rows = layoutGraph({
            commit("c", {"b"}),
            commit("b", {"a"}),
            commit("a", {}),
        });
        QCOMPARE(rows.size(), 3);
        for (const GraphRow &row : rows) {
            QCOMPARE(row.column, 0);
            QCOMPARE(row.laneCount, 1);
        }
        // Tip: no line from above; root: no line below.
        QCOMPARE(rows[0].segments, (QList<GraphSegment>{{0, 0, 0, false}}));
        QCOMPARE(rows[2].segments, (QList<GraphSegment>{{0, 0, 0, true}}));
    }

    void mergeOpensAndClosesSecondLane()
    {
        const QList<GraphRow> rows = layoutGraph({
            commit("m", {"b", "c"}),
            commit("b", {"a"}),
            commit("c", {"a"}),
            commit("a", {}),
        });
        QCOMPARE(rows[0].column, 0);
        QVERIFY(rows[0].segments.contains(GraphSegment{0, 1, 1, false}));

        QCOMPARE(rows[1].column, 0);
        QVERIFY(rows[1].segments.contains(GraphSegment{1, 1, 1, true}));
        QVERIFY(rows[1].segments.contains(GraphSegment{1, 1, 1, false}));

        QCOMPARE(rows[2].column, 1);
        QVERIFY(rows[2].segments.contains(GraphSegment{0, 0, 0, true}));

        // Both lanes wait for "a" and converge into it.
        QCOMPARE(rows[3].column, 0);
        QVERIFY(rows[3].segments.contains(GraphSegment{1, 0, 1, true}));
        QVERIFY(rows[3].segments.contains(GraphSegment{0, 0, 0, true}));
    }

    void workingTreeRowLeadsIntoHead()
    {
        GraphRow wip;
        const QList<GraphRow> rows = layoutGraphWithWip({commit("b", {"a"}), commit("a", {})}, "b", wip);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(wip.column, 0);
        QCOMPARE(wip.segments, (QList<GraphSegment>{{0, 0, 0, false}}));
        // HEAD now has a line coming in from above.
        QVERIFY(rows[0].segments.contains(GraphSegment{0, 0, 0, true}));
    }

    void workingTreeRowReachesHeadBelowOtherTips()
    {
        GraphRow wip;
        const QList<GraphRow> rows = layoutGraphWithWip({commit("x", {"a"}), commit("b", {"a"}), commit("a", {})},
                                                        "b", wip);
        // The other tip takes a lane of its own; the WIP lane passes it.
        QCOMPARE(rows[0].column, 1);
        QVERIFY(rows[0].segments.contains(GraphSegment{0, 0, 0, true}));
        QCOMPARE(rows[1].column, 0);
    }

    void secondBranchTipGetsFreeLane()
    {
        const QList<GraphRow> rows = layoutGraph({
            commit("x", {"a"}),
            commit("y", {"a"}),
            commit("a", {}),
        });
        QCOMPARE(rows[0].column, 0);
        QCOMPARE(rows[1].column, 1);
        QCOMPARE(rows[1].laneCount, 2);
        QCOMPARE(rows[2].column, 0);
        QVERIFY(rows[2].segments.contains(GraphSegment{1, 0, 1, true}));
    }
};

QTEST_GUILESS_MAIN(TestGraphLayout)
#include "tst_graphlayout.moc"
