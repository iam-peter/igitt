# igitt

A Qt Quick git tool combining what gitk (history browsing) and git gui
(staging and committing) do, plus the operations neither of them offers —
first of all folding working tree changes into any earlier commit.

## Building

Requires Qt ≥ 6.9, CMake ≥ 3.21 and git ≥ 2.40 at runtime. KDE's
KSyntaxHighlighting (KF6) is used for highlighting if CMake finds it, built
against the same Qt; `-DIGITT_USE_KSYNTAXHIGHLIGHTING=OFF` leaves it out.

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
  values on the git thread, from the repository or a plan's simulation.
- `src/core/PlanSession.*` — plan mode: simulates actions, replays them,
  publishes the result.
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

## History

The commit graph follows GitKraken's and GitLens': branch and tag labels in their own
column, colored like their lane and linked to the commit; commits drawn
with the author's initials; uncommitted changes as a "// WIP" row on top,
connected to HEAD; and a Changes column with the number of files and a
bar of added and removed lines, loaded for the rows on screen. Columns
can be resized by dragging their header edge and picked from the View
menu or by right-clicking the header. Above it, an activity minimap shows
commits over time, the rows on screen and the selection; click it to
jump. The search takes `author:`, `message:` and `sha:`, quoted phrases,
and optional case-sensitive or regular-expression matching; Enter and
Shift+Enter jump between matches. The scope selector limits the graph to
the current branch.

## Time-lapse view

Open a file's Time-lapse View from the **Files** tab (double-click a file
of the project tree, or filter it by name first), or right-click a file
in the change lists or a file header in any diff. It is modelled on Perforce's: a slider through every revision
of the file (following renames), and the file as it was at that revision
with each line blamed to the revision that last changed it, shaded by
age. The lines the revision changed are highlighted, the ones it deleted
shown struck through; clicking a line's revision jumps there. The arrow
keys step through the revisions, and Play animates them.

## Fetch and push

**Fetch** fetches the current branch's remote (all remotes without an
upstream) and prunes deleted branches; the button says when it last ran.
**Push** updates the upstream branch, setting one on the first push. For
Gerrit it becomes **Push for Review** and pushes `HEAD:refs/for/<branch>`:
detected from a `refs/for/` push refspec on the remote, a commit-msg hook
that adds Change-Id, or Gerrit's SSH port, or chosen in Settings, where
the review branch can be set too. Gerrit's change links are shown after
pushing. A rejected push offers a force-push that uses
`--force-with-lease`, so it only replaces what was last fetched.

igitt cannot answer password prompts: SSH runs in batch mode, so keys
need to be in ssh-agent, and HTTPS needs a credential helper. Both
buttons are off while planning, and Push during a merge or rebase.

## Resolving conflicts

A conflicted file opens in the merge tool instead of a diff, after
GitKraken's: our side and theirs next to each other, labelled with their
commits, and a checkbox on each conflict to take its lines. Taking both
keeps them in the order they were picked. The output below follows the
choices and can be edited before **Save and Mark Resolved** writes and
stages it. Files that cannot be merged line by line (binary, or deleted
on one side) offer keeping one side instead. Folds and plans wait until
the merge, rebase or cherry-pick is finished.

## Commit messages

The commit form has a title with a character counter, a body with a guide
at the wrap width, and trailer rows such as `Task-number: QTBUG-123`. The
message is put together as title, blank line, body, blank line, and one
contiguous trailer block. Settings… sets the title limit (50), the body
width (72), whether the body is wrapped on commit (bullets keep a hanging
indent; indented code and long URLs are left alone), and the trailer keys
the form offers. Amend splits the existing message back into the form,
trailers like Change-Id included.

## Reading diffs

Diffs and files from the Files tab are highlighted: with KSyntaxHighlighting
when igitt is built with it, otherwise by a small built-in highlighter for
C++, QML/JavaScript, CMake, Python and Markdown. Where a removed line is
followed by its replacement, the words that changed are marked more
strongly, as on GitHub. Both are worked out on the git thread, and only up
to a limit (30,000 highlighted lines, 5,000 compared pairs, 2,000
characters per line), so big diffs stay quick. Lines are drawn as plain
text; nothing in a file is ever read as markup.

The diff toolbar turns highlighting and word marks off and on, and can
ignore changes of white space (`git diff -w`). Lines can't be staged or
folded while white space is ignored: such a diff would not apply back.

## Staging

As in git gui: click lines to select them (Shift+click for a range, a hunk
header for the whole hunk) and stage or unstage the selection, or
right-click a line for Stage Hunk and Stage Line. Double-click a file, or
click its status badge, to stage or unstage all of it.

## Plan mode

With **Plan** switched on, write actions (staging, unstaging, commit,
amend, fold) are not carried out but collected in a plan. Each one is
simulated on a scratch copy of the index and a simulated HEAD, so the
history, file lists and diffs show the result, with planned commits
tagged. Every entry lists the git commands it ran, with patches and
messages; removing one replays the rest, and flags entries that no longer
apply.

None of these actions touch the working tree, which makes the simulation
exact. **Execute** therefore runs nothing again: it publishes the reviewed
result, moving HEAD with `git update-ref` and replacing the index under
git's own `index.lock`. It refuses, changing nothing, if HEAD or the index
were changed by anything else since planning began.

Commit hooks (pre-commit, prepare-commit-msg, commit-msg) run when a
commit is planned, so the plan shows the message they produce, Change-Id
included. Staging a file records the file as it was at that moment.

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

## Appearance

The interface uses Inter and code JetBrains Mono, both bundled; Settings
can change either and their sizes. Files in a diff collapse and expand by
clicking their header, or all at once from the diff's toolbar.

## Third-party assets

- Icons: [Lucide](https://lucide.dev), ISC license (`src/icons/LICENSE.txt`).
- Fonts: [Inter](https://rsms.me/inter/) and
  [JetBrains Mono](https://www.jetbrains.com/lp/mono/), SIL Open Font
  License 1.1 (`src/fonts/`).
