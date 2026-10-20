# igitt

A Qt Quick git tool combining what gitk (history browsing) and git gui
(staging and committing) do, plus the operations neither of them offers —
first of all folding working tree changes into any earlier commit.

## Building

Requires Qt ≥ 6.8, CMake ≥ 3.21 and git ≥ 2.40 at runtime.

```bash
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/igitt [path/to/repo]
```

`tst_ui` saves screenshots of the window when `IGITT_SCREENSHOT_DIR` is set.

## Layout

- `src/core` — no Qt GUI dependency: `GitRunner` (runs the git CLI),
  parsers for log, refs, status and diffs, the graph lane layout, and
  `PatchBuilder`, which turns a line selection into a patch for `git apply`.
- `src/models` — list models over that data for QML.
- `src/core/StateLoader.*` — reads status, history and diffs into plain
  values on the git thread.
- `src/Repository.*` — the facade QML talks to (QML module `Igitt.Core`).
- `src/qml`, `src/ui` — the UI (QML module `Igitt`), including the
  `CommitGraph` item that paints the history lanes.

All writes go through the git CLI so hooks, config and git's own checks
behave exactly as in a terminal.

git never runs on the UI thread. `Repository` queues every operation as a
job for a single background thread, which runs them one at a time in order:
writes never overlap, and every read sees the writes queued before it. Only
the UI thread touches the models, applying each job's result when it
arrives; results for a diff the user has already left are dropped.

## Staging

As in git gui: click lines to select them (Shift+click for a range, a hunk
header for the whole hunk) and stage or unstage the selection, or
right-click a line for Stage Hunk and Stage Line. Double-click a file, or
click its status badge, to stage or unstage all of it.

## Folding changes into an earlier commit

Select lines in the unstaged or staged diff, then drag the "Drag onto a
commit to fold" handle onto a commit in the history, or right-click the
commit. This is the interactive form of `git commit --fixup=<sha>` followed
by `git rebase -i --autosquash <sha>~`, done by `FoldEngine` without
touching the working tree:

1. Apply the selected lines to their base tree (HEAD for staged lines, the
   index for unstaged ones) in a temporary index.
2. Merge that change into the target with `git merge-tree --write-tree
   --merge-base=<base>` and recreate the target with its author, date and
   message.
3. Replay every later commit the same way.
4. Move HEAD with `git update-ref HEAD <new> <old>`, which fails if the
   branch moved meanwhile. Unstaged lines that were folded are staged, so
   they don't show up as changes any more.

A conflict in steps 1–3 aborts with nothing changed. Commits already on a
remote branch ask for confirmation first. Undo restores the branch and the
index as long as HEAD hasn't moved since; the reflog has every fold too
("igitt: fold into …").

While lines are selected, igitt suggests the commit they belong in, the
way `git absorb` does: `FoldSuggester` blames the lines each change modifies
or borders and picks the newest local commit among them, since folding into
an older one would conflict with it. Local commits are those after the
merge base with the upstream branch and after the last merge commit. The
suggested commit is tagged "fold here" in the history, and "Fold into
suggested" folds into it. A selection spanning several commits, or touching
lines that are only staged, gets no suggestion, with the reason shown
instead.

Not supported yet: merge commits between the target and HEAD, and moving
other branches that point into the rewritten range (like `rebase
--update-refs`).

## Ideas

- Split an ambiguous selection and fold each part into its own commit.
