#pragma once

#include "LogParser.h"

#include <QList>

// One line of the commit graph within a single row. Upper segments run from
// the top edge of the row to its vertical middle (where the commit dot sits),
// lower segments from the middle to the bottom edge.
struct GraphSegment
{
    int from = 0;
    int to = 0;
    int color = 0;
    bool upper = false;

    bool operator==(const GraphSegment &) const = default;
};

struct GraphRow
{
    int column = 0;    // lane holding the commit dot
    int color = 0;     // color of the commit's lane
    int laneCount = 0; // lanes this row touches; the drawing width
    QList<GraphSegment> segments;
};

// Assigns lanes to commits given in topological order (children before
// parents), the way gitk does.
QList<GraphRow> layoutGraph(const QList<CommitInfo> &commits);

// Like layoutGraph(), with a row for uncommitted changes on top that leads
// into `head` (unconnected if empty). That row goes to `wipRow`.
QList<GraphRow> layoutGraphWithWip(const QList<CommitInfo> &commits, const QString &head, GraphRow &wipRow);

// The color of a lane, by GraphRow::color and GraphSegment::color.
QString laneColorName(int color);
