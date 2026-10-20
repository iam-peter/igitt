#pragma once

#include "DiffParser.h"

#include <QByteArray>
#include <QList>

// Selected lines of one file, indexed [hunk][line] like FileDiff::hunks.
using LineSelection = QList<QList<bool>>;

enum class PatchDirection {
    // The patch is applied as is, e.g. `git apply --cached` to stage lines of
    // the unstaged diff: unselected removals stay, unselected additions vanish.
    Forward,
    // The patch is applied with --reverse, e.g. to unstage lines of the
    // staged diff: the new side must stay intact apart from the selection.
    Reverse,
};

// Builds a patch containing only the selected changed lines of `file`, with
// hunk headers recomputed. Returns an empty array if nothing is selected.
QByteArray buildPatch(const FileDiff &file, const LineSelection &selection,
                      PatchDirection direction);

// A selection covering every changed line.
LineSelection selectAll(const FileDiff &file);
