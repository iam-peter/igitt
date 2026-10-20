#pragma once

#include "DiffParser.h"
#include "FoldSuggester.h"
#include "GitRunner.h"
#include "GraphLayout.h"
#include "LogParser.h"
#include "StatusParser.h"

#include <QSet>
#include <QString>

#include <optional>

// Reading repository state into plain values. Everything here runs on the
// git thread and touches no models; the UI thread applies the results.

enum class DiffKind { None, Unstaged, Staged, Commit };

struct DiffRequest
{
    DiffKind kind = DiffKind::None;
    QString path;   // Unstaged, Staged
    QString commit; // Commit
    int generation = 0; // lets the UI drop results for a view it has left
};

struct DiffState
{
    DiffRequest request;
    QList<FileDiff> files;
    QString message; // shown instead of a diff, e.g. for a conflicted file
    QString details; // commit header and message
    FoldSuggester suggester;
    QString error;
};

struct HistoryState
{
    QString head;
    QList<CommitInfo> commits;
    QList<GraphRow> graph;
    QList<RefInfo> refs;
    QString headMessage;
    QStringList foldTargets;    // commits a fold can rewrite, HEAD first
    QSet<QString> unpublished;  // commits on no remote-tracking branch
    QString error;
};

// Any part may be missing when a job had no reason to reload it.
struct RepositoryState
{
    std::optional<StatusInfo> status;
    std::optional<HistoryState> history;
    std::optional<DiffState> diff;
    QString error;
};

// The diff options everything parsing diffs or feeding `git apply` needs,
// whatever the user's diff configuration says.
QStringList diffFormatArgs();

HistoryState loadHistory(const GitRunner &git, const StatusInfo &status);
DiffState loadDiff(const GitRunner &git, const DiffRequest &request, const StatusInfo &status);

// Status; history when HEAD is not `knownHead` or `forceHistory` is set;
// the working tree diff `diff` asks for, if any.
RepositoryState loadState(const GitRunner &git, const DiffRequest &diff, const QString &knownHead,
                          bool forceHistory);
