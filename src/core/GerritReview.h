#pragma once

#include "GitRunner.h"

#include <QHash>
#include <QList>
#include <QString>

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

// A change as `gerrit query --format=JSON --current-patch-set` reports it.
struct GerritChange
{
    QString changeId;
    int number = 0;
    QString url;
    QString status; // NEW, MERGED, ABANDONED
    QString revision; // of the current patch set
    int patchSet = 0;
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
