#pragma once

#include "GitRunner.h"

#include <QList>
#include <QString>

// Commits that may hold work otherwise lost: where HEAD was (its reflog),
// and commits nothing refers to any more (dangling), such as the tip of a
// branch deleted without having been checked out, or a dropped stash.
struct LostCommit
{
    enum Kind { Reflog, Dangling };
    Kind kind = Reflog;
    QString sha;
    QString subject;
    qint64 time = 0;   // the commit's, in seconds since the epoch
    QString selector;  // HEAD@{3}, for a reflog entry
    QString action;    // "checkout: moving from a to b", for a reflog entry
    bool unreachable = false; // no branch, tag or stash has it
    bool stash = false;       // a stash's commit, to be stored again
};

// HEAD's last `reflogLimit` moves, then the dangling commits, newest first
QList<LostCommit> findLostWork(const GitRunner &git, int reflogLimit = 100);
