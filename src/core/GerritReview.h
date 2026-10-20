#pragma once

#include "GitRunner.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

// A Gerrit server reached over SSH, from a remote URL such as
// ssh://user@review.example.org:29418/some/project.
struct GerritRemote
{
    QString user;
    QString host;
    int port = 29418;
    QString project;

    bool isValid() const { return !host.isEmpty() && !project.isEmpty(); }
};

GerritRemote parseGerritRemote(const QString &url);

// A vote on a change's current patch set.
struct GerritVote
{
    QString label; // Code-Review, Verified, ...
    int value = 0;
    QString by;
};

// A message on a change, written by a reviewer, a bot or Gerrit itself on
// an upload or a vote; "Patch Set 2: Code-Review+1\n\n(1 comment)" and so on.
struct GerritMessage
{
    QDateTime date;
    QString author;
    QString text;
};

// An inline comment on a patch set's file; line 0 is on the file as a
// whole. Gerrit's JSON tells neither the side of the diff it is on (it is
// taken as the patch set's own, the new side) nor whether it is resolved.
struct GerritComment
{
    int patchSet = 0;
    QString file;
    int line = 0;
    QString author;
    QString text;
};

struct GerritPatchSet
{
    int number = 0;
    QString revision;
    QString ref; // refs/changes/NN/NNNN/P
    QString uploader;
    QDateTime created;
    QString kind; // REWORK, TRIVIAL_REBASE, NO_CODE_CHANGE, ...
    QList<GerritComment> comments;
};

// A change as `gerrit query --format=JSON --current-patch-set --patch-sets
// --comments` reports it.
struct GerritChange
{
    QString changeId;
    int number = 0;
    QString url;
    QString status; // NEW, MERGED, ABANDONED
    QString revision; // of the current patch set
    int patchSet = 0;
    bool wip = false;
    QString topic;
    QStringList hashtags;
    QList<GerritVote> votes;        // on the current patch set
    QList<GerritComment> comments;  // inline, on the current patch set
    QList<GerritMessage> messages;  // oldest first
    QList<GerritPatchSet> patchSets; // oldest first
};

QHash<QString, GerritChange> parseGerritQuery(const QByteArray &output);

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
    QList<GerritComment> comments; // on the current patch set
    QList<GerritMessage> messages;
    QList<GerritPatchSet> patchSets;
    // The patch set that is this very commit, 0 if none is.
    int localPatchSet = 0;
    // Gerrit's has:unresolved; unknown if that second query failed.
    std::optional<bool> unresolved;

    const GerritPatchSet *findPatchSet(int number) const;

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

struct ReviewQuery
{
    bool ok = false; // Gerrit answered
    QString error;
    QHash<QString, ReviewInfo> bySha; // the commits ahead of the upstream
};

// Asks the Gerrit behind `remote` about the commits between `upstream` and
// HEAD, matched by Change-Id within the project and `branch`. Uses ssh in
// batch mode, honoring GIT_SSH_COMMAND and core.sshCommand.
ReviewQuery queryReviews(const GitRunner &git, const QString &remote, const QString &upstream,
                         const QString &branch);

// "Trivial rebase" for TRIVIAL_REBASE and so on; unknown kinds as they are.
QString patchSetKindText(const QString &kind);
// The ref Gerrit keeps a patch set under: refs/changes/01/101/2.
QString patchSetRef(int change, int patchSet);
