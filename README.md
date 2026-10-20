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
- `src/Repository.*` — the facade QML talks to (QML module `Igitt.Core`).
- `src/qml`, `src/ui` — the UI (QML module `Igitt`), including the
  `CommitGraph` item that paints the history lanes.

All writes go through the git CLI so hooks, config and git's own checks
behave exactly as in a terminal.

## Roadmap: fold changes into an earlier commit

The interactive equivalent of `git commit --fixup=<sha>` followed by
`git rebase -i --autosquash <sha>~`, without touching the working tree:

1. Build the patch from the selected lines (`PatchBuilder`, as for staging).
2. In a temporary index (`GIT_INDEX_FILE`), `read-tree` the target commit,
   `apply --cached` the patch, `write-tree`, and `commit-tree` with the
   target's message and author.
3. Replay the target's descendants onto the new commit with
   `git merge-tree --write-tree --merge-base=<old parent>` and
   `commit-tree`; abort without changes on the first conflict.
4. Move the branch with `git update-ref <ref> <new> <old>`, then remove the
   folded lines from the working tree and index.

In the UI: drag a selection from the diff onto a commit in the history, with
a blame-based suggestion of the target commit as `git absorb` does.
