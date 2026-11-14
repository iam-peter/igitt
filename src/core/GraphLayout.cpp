#include "GraphLayout.h"

#include "LaneColors.h"

#include <QHash>

#include <algorithm>
#include <vector>

namespace {

// Commits by number, not by their 40 characters: lanes are compared for
// every row with every lane open
struct Lane
{
    int commit = -1; // commit this lane is waiting for; -1 if the lane is free
    int color = 0;
};

int findLane(const QList<Lane> &lanes, int commit)
{
    for (int i = 0; i < lanes.size(); ++i) {
        if (lanes[i].commit == commit)
            return i;
    }
    return -1;
}

int freeLane(QList<Lane> &lanes)
{
    for (int i = 0; i < lanes.size(); ++i) {
        if (lanes[i].commit < 0)
            return i;
    }
    lanes.append(Lane{});
    return int(lanes.size()) - 1;
}

} // namespace

QString laneColorName(int color)
{
    return LaneColors::instance()->colorName(color);
}

QList<GraphRow> layoutGraphWithWip(const QList<CommitInfo> &commits, const QString &head, GraphRow &wipRow)
{
    QList<CommitInfo> all;
    all.reserve(commits.size() + 1);
    CommitInfo wip;
    wip.sha = QStringLiteral("(working tree)");
    if (!head.isEmpty())
        wip.parents = {head};
    all.append(wip);
    all += commits;
    QList<GraphRow> rows = layoutGraph(all);
    wipRow = rows.takeFirst();
    return rows;
}

QList<GraphRow> layoutGraph(const QList<CommitInfo> &commits)
{
    // A commit's number is its row; a parent not among the commits gets
    // one after them
    QHash<QString, int> numbers;
    numbers.reserve(commits.size());
    for (int i = 0; i < commits.size(); ++i)
        numbers.insert(commits[i].sha, i);
    const auto numberOf = [&numbers](const QString &sha) {
        auto it = numbers.constFind(sha);
        if (it == numbers.cend())
            it = numbers.insert(sha, int(numbers.size()));
        return it.value();
    };

    QList<Lane> lanes;
    int nextColor = 0;
    QList<GraphRow> rows;
    rows.reserve(commits.size());
    std::vector<char> passing; // lanes that pass by this row, for all rows

    for (int number = 0; number < commits.size(); ++number) {
        const CommitInfo &commit = commits[number];
        GraphRow row;

        int col = findLane(lanes, number);
        const bool hasIncoming = col >= 0;
        if (!hasIncoming) {
            // Branch tip: nothing above leads here.
            col = freeLane(lanes);
            lanes[col] = {number, nextColor++};
        }
        row.column = col;
        row.color = lanes[col].color;
        // Each lane in and out, and a segment for each further parent
        row.segments.reserve(2 * lanes.size() + commit.parents.size() + 1);

        // Upper half: every lane coming in from the row above either ends in
        // this commit or passes by.
        passing.assign(lanes.size(), 0);
        for (int i = 0; i < lanes.size(); ++i) {
            const Lane &lane = lanes[i];
            if (lane.commit < 0 || (i == col && !hasIncoming))
                continue;
            if (lane.commit == number) {
                row.segments.append({i, col, lane.color, true});
            } else {
                row.segments.append({i, i, lane.color, true});
                passing[i] = 1;
            }
        }
        for (int i = 0; i < lanes.size(); ++i) {
            if (i != col && lanes[i].commit == number)
                lanes[i].commit = -1;
        }

        // Lower half: the first parent continues this lane, further parents
        // join a lane already waiting for them or open a new one.
        if (commit.parents.isEmpty()) {
            lanes[col].commit = -1;
        } else {
            lanes[col].commit = numberOf(commit.parents.first());
            row.segments.append({col, col, lanes[col].color, false});
            for (qsizetype p = 1; p < commit.parents.size(); ++p) {
                const int parent = numberOf(commit.parents[p]);
                int target = findLane(lanes, parent);
                if (target < 0) {
                    target = freeLane(lanes);
                    lanes[target] = {parent, nextColor++};
                }
                row.segments.append({col, target, lanes[target].color, false});
            }
        }
        for (int i = 0; i < int(passing.size()); ++i) {
            if (passing[i])
                row.segments.append({i, i, lanes[i].color, false});
        }

        row.laneCount = col + 1;
        for (const GraphSegment &s : std::as_const(row.segments))
            row.laneCount = std::max({row.laneCount, s.from + 1, s.to + 1});

        while (!lanes.isEmpty() && lanes.last().commit < 0)
            lanes.removeLast();

        rows.append(row);
    }
    return rows;
}
