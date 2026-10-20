#pragma once

#include "ReviewProvider.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

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

// A change as `gerrit query --format=JSON --current-patch-set --comments`
// reports it.
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
    QList<GerritVote> votes; // on the current patch set
    int comments = 0;        // inline comments on the current patch set
};

QHash<QString, GerritChange> parseGerritQuery(const QByteArray &output);

// Gerrit: a refs/for/ push refspec, a commit-msg hook adding Change-Id, or
// Gerrit's SSH port in the URL.
bool looksLikeGerrit(const GitRunner &git, const QString &remote);

// "%topic=x,wip,r=a@b,t=tag", or empty without options. A value a refspec
// cannot carry, such as one with a space or comma, sets `error` instead.
QString reviewRefspecOptions(const ReviewOptions &options, QString *error);

// Asks the Gerrit behind `remote` about the commits between `upstream` and
// HEAD, matched by Change-Id within the project and `branch`. Uses ssh in
// batch mode, honoring GIT_SSH_COMMAND and core.sshCommand.
ReviewQuery queryGerritReviews(const GitRunner &git, const QString &remote, const QString &upstream,
                               const QString &branch);

const ReviewProvider &gerritProvider();
