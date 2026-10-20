#pragma once

#include "GitRunner.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <optional>

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

// Gerrit push options, sent in the refspec:
// HEAD:refs/for/<branch>%topic=x,wip,r=a@b,t=tag
struct ReviewOptions
{
    enum class State { Unchanged, Wip, Ready };

    QString topic; // none, or clears the remembered one
    State state = State::Unchanged;
    QStringList reviewers;
    QStringList hashtags;
};

// "%topic=x,wip,r=a@b,t=tag", or empty without options. A value a refspec
// cannot carry, such as one with a space or comma, sets `error` instead.
QString reviewRefspecOptions(const ReviewOptions &options, QString *error);
// Splits "a, b c" into {"a", "b", "c"}.
QStringList splitReviewList(const QString &text);

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
    QString branch;         // the local branch; empty if HEAD is detached
    // Gerrit: the topic last pushed with from this branch, and the options
    // this push sends, if any.
    QString savedTopic;
    std::optional<ReviewOptions> review;

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
// `review` applies to pushes for review only.
PushTarget resolvePushTarget(const GitRunner &git, const PushSettings &settings,
                             const std::optional<ReviewOptions> &review = std::nullopt);
// The remote of the current branch's upstream; empty if there is none.
QString upstreamRemote(const GitRunner &git);

// `force` uses --force-with-lease, pinned to the remote-tracking branch as
// last fetched, so nothing the user has not seen gets overwritten. A push
// for review with options remembers its topic in branch.<name>.igittTopic.
NetworkResult pushTo(const GitRunner &git, const PushTarget &target, bool force);
// Fetches `remote`, or all remotes if empty, pruning deleted branches.
NetworkResult fetchFrom(const GitRunner &git, const QString &remote);
// Fetches the objects of `ref` from `remote` without writing any ref or
// FETCH_HEAD, e.g. a Gerrit patch set to compare with.
NetworkResult fetchObjects(const GitRunner &git, const QString &remote, const QString &ref);

QStringList remoteLinks(const QString &stderrText);
// Turns git's errors about credentials and SSH into advice; igitt cannot
// answer password prompts.
QString explainNetworkFailure(const QString &stderrText);
