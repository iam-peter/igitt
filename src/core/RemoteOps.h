#pragma once

#include "GitRunner.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <optional>

enum class PushMode {
    Auto,   // the review host the remote looks like, plain otherwise
    Plain,  // update the upstream branch, no review host
    Gerrit, // push HEAD for review to refs/for/<branch>
    GitHub, // update the upstream branch; it has pull requests
    GitLab, // update the upstream branch, with merge request push options
};

// Where reviews happen for a remote; see detectReviewHost().
enum class ReviewHost { None, Gerrit, GitHub, GitLab };

struct PushSettings
{
    PushMode mode = PushMode::Auto;
    QString gerritBranch; // target branch for reviews; the upstream's if empty
    // Self-hosted servers Auto recognizes besides github.com, gitlab.com
    // and hosts named github.* or gitlab.*.
    QStringList githubHosts;
    QStringList gitlabHosts;
};

// Options for one push. Each review host uses its own: Gerrit sends them in
// the refspec (HEAD:refs/for/<branch>%topic=x,wip,r=a@b,t=tag), GitLab as
// push options (-o merge_request.create), and GitHub has none, so igitt
// opens the pull request with `gh pr create` after pushing.
struct ReviewOptions
{
    enum class State { Unchanged, Wip, Ready }; // Wip is a draft on GitHub and GitLab

    QString topic; // Gerrit: none, or clears the remembered one
    State state = State::Unchanged;
    QStringList reviewers; // Gerrit, GitHub
    QStringList hashtags;  // Gerrit
    // GitHub and GitLab: open a pull or merge request, into `target` or
    // the default branch, titled `title` or after the commits.
    bool create = false;
    QString target;
    QString title;
    QStringList labels;
    bool removeSourceBranch = false; // GitLab, once merged
};

// Splits "a, b c" into {"a", "b", "c"}.
QStringList splitReviewList(const QString &text);

// Where a push goes, worked out from the current branch, its upstream and
// the repository's configuration.
struct PushTarget
{
    ReviewHost host = ReviewHost::None;
    bool forReview = false; // Gerrit: to refs/for/, not the branch itself
    QString remote;
    QString remoteBranch;   // the branch updated, or reviewed for
    QStringList args;       // for `git push`, after the options
    bool setsUpstream = false;
    QString error;          // why there is nowhere to push to
    QString branch;         // the local branch; empty if HEAD is detached
    // Gerrit: the topic last pushed with from this branch.
    QString savedTopic;
    // The options this push sends, if any.
    std::optional<ReviewOptions> review;

    QString description() const;
};

struct NetworkResult
{
    bool ok = false;
    bool rejected = false;  // the remote refused the update, e.g. not a fast-forward
    QString message;        // what went wrong, or the remote's reply
    QStringList links;      // URLs the remote printed, e.g. Gerrit changes
    // Pushed, but what the review host was to do afterwards failed.
    QString warning;
};

// `review` is applied by the remote's review host; see ReviewProvider.
PushTarget resolvePushTarget(const GitRunner &git, const PushSettings &settings,
                             const std::optional<ReviewOptions> &review = std::nullopt);
// The remote of the current branch's upstream; empty if there is none.
QString upstreamRemote(const GitRunner &git);

// `force` uses --force-with-lease, pinned to the remote-tracking branch as
// last fetched, so nothing the user has not seen gets overwritten. After a
// push, the review host finishes what the options ask for.
NetworkResult pushTo(const GitRunner &git, const PushTarget &target, bool force);
// Fetches `remote`, or all remotes if empty, pruning deleted branches.
NetworkResult fetchFrom(const GitRunner &git, const QString &remote);

QStringList remoteLinks(const QString &stderrText);
// Turns git's errors about credentials and SSH into advice; igitt cannot
// answer password prompts.
QString explainNetworkFailure(const QString &stderrText);
