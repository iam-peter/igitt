#pragma once

#include "DiffParser.h"
#include "GitRunner.h"

#include <QDateTime>
#include <QList>
#include <QString>

// One revision of a file, as `git log --follow` lists it.
struct FileRevision
{
    QString sha;
    QString path; // the file's name in this revision
    QString author;
    QDateTime date;
    QString subject;
};

// What a revision did to a line. A removed and an added line that pair up
// as an edit (editedLines(), the lines the diff view marks words in) are
// one line changed; the others are removed or added.
enum class LineChange { Kept, Removed, Added, Changed };

// A line of a file at some revision, for the time-lapse view.
struct TimelapseLine
{
    LineChange change = LineChange::Kept; // by the revision on display; Removed: not in the file
    QString text;
    int lineNo = 0;      // not for Removed
    int revision = -1;   // index into the revisions the line was last changed in; -1 if unknown
    QString sha;         // that commit
    QString author;
};

// How the lines of a file go from one revision to the next: a run of
// lines kept or changed, moving from their old place to their new one,
// removed (from their old place, gone at `newStart` of the next revision)
// or added (at their new place, coming in at `oldStart` of the one
// before). Places are 0-based line numbers; a run that is not in a file
// has its count 0 there.
struct FlowRun
{
    LineChange kind = LineChange::Kept;
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
};

// A revision of a file in its flow: how long the file is then, how many
// lines the revision added, changed and removed, and the runs from the
// revision before. The first revision has its whole file added.
struct FlowStep
{
    int lines = 0;
    int added = 0;
    int changed = 0;
    int removed = 0;
    bool binary = false;
    QList<FlowRun> runs;
};

// The runs a diff without context (-U0) makes of a file of `oldLines`
// lines, in their order in the file.
QList<FlowRun> flowRuns(const QList<DiffHunk> &hunks, int oldLines);

// The flow of a file through `revisions` (as loadFileRevisions() gives
// them), one step for each: each revision against the one before it,
// whatever lies between them in the graph.
QList<FlowStep> loadFileFlow(const GitRunner &git, const QList<FileRevision> &revisions);

// The revisions of `path` reachable from `head`, oldest first, following
// renames.
QList<FileRevision> loadFileRevisions(const GitRunner &git, const QString &path,
                                      const QString &head = QStringLiteral("HEAD"));

// The file at `revisions[index]`, each line blamed to the revision that
// last changed it and marked with what that revision did to it, against
// the one before it as in the flow. With `withDeleted`, the lines it
// removed are listed where they used to be.
QList<TimelapseLine> loadTimelapse(const GitRunner &git, const QList<FileRevision> &revisions, int index,
                                   bool withDeleted, QString *error = nullptr);
