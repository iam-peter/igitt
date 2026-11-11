#pragma once

#include "GitRunner.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

// Commits a patch on its own, apart from the index: what composing commits
// from clusters of changes does with each cluster.

// The paths a patch changes.
QStringList patchPaths(const QByteArray &patch);

// The tree of `base` (a commit or a tree; empty for none) with `patch`
// applied, built in an index of its own. With `threeWay`, a patch made on
// a slightly different state is merged in through its preimage, as a plan
// replaying it after an earlier entry went may need. Empty, with `error`
// set, if the patch does not apply.
QString treeWithPatch(const GitRunner &git, const QString &base, const QByteArray &patch, QString &error,
                      bool threeWay = false);

// Commits `patch` on HEAD with `message`. The commit is made by git commit
// on a scratch index holding HEAD and the patch, so hooks and signing apply
// as in a terminal, and nothing else of the index or the working tree goes
// into it. Afterwards the index entries of the patch's paths follow the new
// HEAD; what else is staged stays staged. Refuses while a merge, a
// cherry-pick or a revert is in progress, which git commit would conclude
// with the patch alone.
//
// Returns the new commit's id, or an empty string with `error` set. With
// an id, `error` may still say that the index could not follow.
QString commitPatch(const GitRunner &git, const QByteArray &patch, const QString &message, QString &error);
