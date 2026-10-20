#pragma once

#include "DiffMarkup.h"
#include "DiffParser.h"
#include "FoldSuggester.h"
#include "GitRunner.h"
#include "GraphLayout.h"
#include "LogParser.h"
#include "RemoteOps.h"
#include "StatusParser.h"

#include <QSet>
#include <QString>

#include <optional>

// Reading repository state into plain values. Everything here runs on the
// git thread and touches no models; the UI thread applies the results.

// Where state is read from: the repository itself, or a plan's simulation
// of it, with a scratch index (set in git's environment) and a commit
// standing in for HEAD.
struct StateSource
{
    GitRunner git;
    QString head = QStringLiteral("HEAD");
    bool simulated = false;
    QString baselineHead; // simulated: commits it does not reach are planned
};

enum class DiffKind { None, Unstaged, Staged, Commit, Content };

struct DiffRequest
{
    DiffKind kind = DiffKind::None;
    QString path;   // Unstaged, Staged, Content
    QString commit; // Commit
    int generation = 0; // lets the UI drop results for a view it has left
    // `git diff -w`. Such a diff cannot be applied back, so no line of it
    // can be staged.
    bool ignoreWhitespace = false;
};

struct DiffState
{
    DiffRequest request;
    QList<FileDiff> files;
    QList<FileMarkup> markup; // parallel to files
    QString message; // shown instead of a diff, e.g. for a conflicted file
    QString details; // commit header and message
    bool conflicted = false; // the file has unresolved merge conflicts
    QStringList content;     // Content: the file's lines
    QList<LineMarkup> contentMarkup;
    bool truncated = false;  // Content: only the first lines
    FoldSuggester suggester;
    QString error;
};

// Which refs the history shows besides local branches and HEAD.
struct HistoryFilter
{
    bool remotes = true;
    bool tags = true;
    bool currentBranchOnly = false; // only what HEAD reaches
};

// When loadState() reloads the history: if `force` is set, or if HEAD or
// the presence of uncommitted changes differs from what the UI shows.
struct HistoryRequest
{
    bool force = false;
    QString knownHead;
    bool knownWip = false;
    HistoryFilter filter;
    PushSettings push;
};

struct HistoryState
{
    QString head;
    bool wip = false; // uncommitted changes, shown as a row on top
    GraphRow wipGraph;
    QList<CommitInfo> commits;
    QList<GraphRow> graph;
    QList<RefInfo> refs;
    QString headMessage;
    QStringList foldTargets;    // commits a fold can rewrite, HEAD first
    QSet<QString> unpublished;  // commits on no remote-tracking branch
    QSet<QString> planned;      // simulated commits that don't exist for real yet
    PushTarget pushTarget;
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

// `git status` cannot be pointed at another HEAD or index, so a simulation
// assembles the same information from plumbing commands.
StatusInfo loadSimulatedStatus(const StateSource &source);
HistoryState loadHistory(const StateSource &source, const StatusInfo &status, const HistoryFilter &filter = {},
                         const PushSettings &push = {});
DiffState loadDiff(const StateSource &source, const DiffRequest &request, const StatusInfo &status);

// Status; history as `history` asks; the working tree diff `diff` asks
// for, if any.
RepositoryState loadState(const StateSource &source, const DiffRequest &diff, const HistoryRequest &history);

QHash<QString, CommitStats> loadCommitStats(const GitRunner &git, const QStringList &shas);
