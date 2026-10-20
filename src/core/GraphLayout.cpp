#include "GraphLayout.h"

#include <algorithm>

namespace {

struct Lane
{
    QString sha; // commit this lane is waiting for; empty if the lane is free
    int color = 0;
};

int findLane(const QList<Lane> &lanes, const QString &sha)
{
    for (int i = 0; i < lanes.size(); ++i) {
        if (lanes[i].sha == sha)
            return i;
    }
    return -1;
}

int freeLane(QList<Lane> &lanes)
{
    for (int i = 0; i < lanes.size(); ++i) {
        if (lanes[i].sha.isEmpty())
            return i;
    }
    lanes.append(Lane{});
    return int(lanes.size()) - 1;
}

} // namespace

QList<GraphRow> layoutGraph(const QList<CommitInfo> &commits)
{
    QList<Lane> lanes;
    int nextColor = 0;
    QList<GraphRow> rows;
    rows.reserve(commits.size());

    for (const CommitInfo &commit : commits) {
        GraphRow row;

        int col = findLane(lanes, commit.sha);
        const bool hasIncoming = col >= 0;
        if (!hasIncoming) {
            // Branch tip: nothing above leads here.
            col = freeLane(lanes);
            lanes[col] = {commit.sha, nextColor++};
        }
        row.column = col;
        row.color = lanes[col].color;

        // Upper half: every lane coming in from the row above either ends in
        // this commit or passes by.
        QList<bool> passing(lanes.size(), false);
        for (int i = 0; i < lanes.size(); ++i) {
            const Lane &lane = lanes[i];
            if (lane.sha.isEmpty() || (i == col && !hasIncoming))
                continue;
            if (lane.sha == commit.sha) {
                row.segments.append({i, col, lane.color, true});
            } else {
                row.segments.append({i, i, lane.color, true});
                passing[i] = true;
            }
        }
        for (int i = 0; i < lanes.size(); ++i) {
            if (i != col && lanes[i].sha == commit.sha)
                lanes[i].sha.clear();
        }

        // Lower half: the first parent continues this lane, further parents
        // join a lane already waiting for them or open a new one.
        if (commit.parents.isEmpty()) {
            lanes[col].sha.clear();
        } else {
            lanes[col].sha = commit.parents.first();
            row.segments.append({col, col, lanes[col].color, false});
            for (qsizetype p = 1; p < commit.parents.size(); ++p) {
                int target = findLane(lanes, commit.parents[p]);
                if (target < 0) {
                    target = freeLane(lanes);
                    lanes[target] = {commit.parents[p], nextColor++};
                }
                row.segments.append({col, target, lanes[target].color, false});
            }
        }
        for (int i = 0; i < passing.size(); ++i) {
            if (passing[i])
                row.segments.append({i, i, lanes[i].color, false});
        }

        row.laneCount = col + 1;
        for (const GraphSegment &s : std::as_const(row.segments))
            row.laneCount = std::max({row.laneCount, s.from + 1, s.to + 1});

        while (!lanes.isEmpty() && lanes.last().sha.isEmpty())
            lanes.removeLast();

        rows.append(row);
    }
    return rows;
}
