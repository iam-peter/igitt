#pragma once

#include "GitRunner.h"
#include "RemoteOps.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

// The review hosts igitt knows: Gerrit reviews each commit as a change,
// GitHub and GitLab review a branch as a pull or merge request. A provider
// says how to push to its host and asks it about reviews, the latter on a
// thread of its own: providers keep no state and every call gets what it
// needs.

// A remote URL in any of git's forms: https://host/path, ssh://user@host:port/path
// or user@host:path. `path` has no leading slash and no ".git".
struct RemoteUrl
{
    QString user;
    QString host;
    int port = 0;
    QString path;

    bool isValid() const { return !host.isEmpty() && !path.isEmpty(); }
};

RemoteUrl parseRemoteUrl(const QString &url);
// The URL of `remote`, as configured.
QString remoteUrl(const GitRunner &git, const QString &remote);

// Which review host `remote` belongs to: what `settings` says, or for
// Auto, Gerrit if the repository looks like it, then GitHub or GitLab by
// the remote's host name.
ReviewHost detectReviewHost(const GitRunner &git, const QString &remote, const PushSettings &settings);

// A vote on a change's current patch set.
struct GerritVote
{
    QString label; // Code-Review, Verified, ...
    int value = 0;
    QString by;
};

// What Gerrit has for one of the commits a push for review would send.
struct ReviewInfo
{
    enum State {
        New,      // no change with its Change-Id yet
        UpToDate, // the current patch set is this very commit
        Outdated, // pushing uploads a new patch set
        Closed,   // merged or abandoned
    };

    State state = New;
    int number = 0;
    int patchSet = 0;
    QString url;
    bool wip = false;
    QString topic;
    QStringList hashtags;
    QList<GerritVote> votes;
    int comments = 0;
    // Gerrit's has:unresolved; unknown if that second query failed.
    std::optional<bool> unresolved;

    // The lowest and highest vote on `label`, 0 for none.
    std::pair<int, int> voteRange(const QString &label) const;
    // The vote Gerrit would show for `label`: a veto beats approvals.
    int shownVote(const QString &label) const;
    // Short for the commit row, e.g. "WIP · CR+2 · V-1"; empty if there
    // is nothing to tell.
    QString badges() const;
    // Everything above, a line each, for a tooltip.
    QString details() const;
};

// A GitHub pull request or GitLab merge request for a branch.
struct BranchReview
{
    enum class State { Open, Merged, Closed };
    enum class Decision { None, Approved, ChangesRequested, ReviewRequired };
    enum class Checks { None, Pending, Success, Failure };

    ReviewHost host = ReviewHost::GitHub;
    int number = 0;
    QString title;
    QString url;
    QString target;  // the branch it goes into
    QString headSha; // the commit it has
    State state = State::Open;
    bool draft = false;
    Decision decision = Decision::None;
    QStringList approvedBy;
    QStringList changesRequestedBy;
    int approvalsLeft = 0; // GitLab: approvals still required
    Checks checks = Checks::None;
    // Per check or pipeline: "passed", "failed", "running", ... by name.
    QList<std::pair<QString, QString>> checkResults;
    bool conflicts = false; // with the target branch
    // Unresolved review threads; unknown if that could not be asked.
    std::optional<int> unresolved;

    // "#12" on GitHub, "!12" on GitLab.
    QString reference() const;
    // "PR #12" or "MR !12".
    QString label() const;
    // Short for the commit row, e.g. "draft · approved · checks failed";
    // empty if there is nothing to tell.
    QString badges() const;
    // Everything above, a line each, for a tooltip. `head` is the local
    // branch's commit, to tell whether the review has it.
    QString details(const QString &head) const;
    // One sentence, e.g. "Pull request #12 is open: approved, checks
    // passed."
    QString summary() const;
};

// What a review host was asked for.
struct ReviewContext
{
    QString remote;
    QString upstream; // Gerrit: the commits after it are pushed for review
    QString branch;   // Gerrit: the target branch; otherwise the remote branch reviewed
    QString head;     // the local branch's commit
};

struct ReviewQuery
{
    ReviewHost host = ReviewHost::None;
    bool ok = false; // the host answered
    QString error;
    QHash<QString, ReviewInfo> bySha; // Gerrit: the commits ahead of the upstream
    // GitHub, GitLab: the branch's pull or merge request, if it has one.
    std::optional<BranchReview> branchReview;
    QString branch; // the branch asked about
    QString head;   // and its local commit
};

class ReviewProvider
{
public:
    virtual ~ReviewProvider() = default;

    virtual ReviewHost host() const = 0;
    // "Gerrit", "GitHub", "GitLab".
    virtual QString name() const = 0;
    // Gerrit: pushes go to refs/for/<branch> rather than update it.
    virtual bool pushesForReview() const { return false; }
    // Fills in what the host keeps for the branch, e.g. Gerrit's topic.
    virtual void completeTarget(const GitRunner &, PushTarget &) const {}
    // Turns `options` into push arguments, or sets target.error if the
    // options cannot be sent. Needs no git, so the UI can preview it.
    virtual void applyOptions(PushTarget &target, const ReviewOptions &options) const = 0;
    // What a push with the target's options sends, for the options popup.
    virtual QString preview(const PushTarget &target) const = 0;
    // After a successful push: what the options ask the host to do then.
    virtual void afterPush(const GitRunner &, const PushTarget &, NetworkResult &) const {}
    // Asks the host; blocks for a network round trip or more.
    virtual ReviewQuery query(const GitRunner &git, const ReviewContext &context) const = 0;
};

// The provider for `host`; nullptr for ReviewHost::None.
const ReviewProvider *reviewProvider(ReviewHost host);

// Runs a host's command line tool (gh, glab, ssh) in `git`'s work tree
// without a terminal, with `env` added; false and `error` if it could not
// start, failed or timed out. `missing` explains a tool that is not
// installed.
bool runReviewTool(const GitRunner &git, const QString &program, const QStringList &args,
                   const QProcessEnvironment &env, const QString &host, const QString &missing,
                   QByteArray *out, QString *error);

// `words` as a shell would need them typed, for showing commands.
QString shellCommand(const QStringList &words);
