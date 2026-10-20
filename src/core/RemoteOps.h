#pragma once

#include "GitRunner.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

enum class PushMode {
    Auto,   // Gerrit if the repository looks like it, plain otherwise
    Plain,  // update the upstream branch
    Gerrit, // push HEAD for review to refs/for/<branch>
};

struct PushSettings
{
    PushMode mode = PushMode::Auto;
    QString gerritBranch; // target branch for reviews; the upstream's if empty
};

// Where a push goes, worked out from the current branch, its upstream and
// the repository's configuration.
struct PushTarget
{
    bool gerrit = false;
    QString remote;
    QString remoteBranch;   // the branch updated, or reviewed for
    QStringList args;       // for `git push`, after the options
    bool setsUpstream = false;
    QString error;          // why there is nowhere to push to

    QString description() const;
};

struct NetworkResult
{
    bool ok = false;
    bool rejected = false;  // the remote refused the update, e.g. not a fast-forward
    QString message;        // what went wrong, or the remote's reply
    QStringList links;      // URLs the remote printed, e.g. Gerrit changes
};

// Gerrit: a refs/for/ push refspec, a commit-msg hook adding Change-Id, or
// Gerrit's SSH port in the URL.
bool looksLikeGerrit(const GitRunner &git, const QString &remote);
PushTarget resolvePushTarget(const GitRunner &git, const PushSettings &settings);
// The remote of the current branch's upstream; empty if there is none.
QString upstreamRemote(const GitRunner &git);

// `force` uses --force-with-lease, pinned to the remote-tracking branch as
// last fetched, so nothing the user has not seen gets overwritten.
NetworkResult pushTo(const GitRunner &git, const PushTarget &target, bool force);
// Fetches `remote`, or all remotes if empty, pruning deleted branches.
NetworkResult fetchFrom(const GitRunner &git, const QString &remote);

QStringList remoteLinks(const QString &stderrText);
// Turns git's errors about credentials and SSH into advice; igitt cannot
// answer password prompts.
QString explainNetworkFailure(const QString &stderrText);
