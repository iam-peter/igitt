#pragma once

#include "FoldEngine.h"
#include "GitRunner.h"

#include <QByteArray>
#include <QString>

// Moves a change between two places of the current branch: a commit, or
// the working tree (an empty id). The change is `patch`, made against
// what it is in: the parent of its commit, or `head` for the working tree.
//
//  - From the working tree into a commit: a fold (FoldEngine).
//  - Out of a commit into the working tree: the commit is folded with the
//    change taken back, and the commits after it are replayed. The working
//    tree is not touched, so the change is in it still, uncommitted now.
//  - From one commit into another: both, the second on the result of the
//    first. The tree at the end is the one at the start.
//
// Nothing visible changes before HEAD moves, and HEAD moves only with
// `moveHead` and only if it is still at `head`; a conflict on the way
// leaves the repository as it was. The index is left alone: the caller
// lets the entries of the patch's paths follow the new HEAD.
FoldResult moveChange(const GitRunner &git, const QString &from, const QString &to, const QByteArray &patch,
                      const QString &head = QStringLiteral("HEAD"), bool moveHead = true);
