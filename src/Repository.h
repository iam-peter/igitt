#pragma once

#include "core/FoldSuggester.h"
#include "core/ReviewProvider.h"
#include "core/GitRunner.h"
#include "core/PlanSession.h"
#include "core/StateLoader.h"
#include "models/CommitModel.h"
#include "models/DiffModel.h"
#include "models/FileListModel.h"
#include "models/FileTreeModel.h"
#include "models/PlanModel.h"

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

// The repository as the UI sees it: history, working tree status and the
// diff currently on display, plus the operations that change them.
//
// git never runs on the UI thread. Every invokable method queues a job for a
// single git thread, which runs jobs one at a time in the order they were
// queued, so a write never overlaps another and a read sees the effects of
// every write queued before it. Results come back through the properties
// and signals.
class Repository : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool valid READ valid NOTIFY pathChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // A queued or running job changes the repository; actions working on
    // what is on screen should wait for it.
    Q_PROPERTY(bool writing READ writing NOTIFY busyChanged)
    Q_PROPERTY(QString branch READ branch NOTIFY headChanged)
    // The local branches, and the remote-tracking ones the history shows
    Q_PROPERTY(QStringList branches READ branches NOTIFY headChanged)
    Q_PROPERTY(QStringList remoteBranches READ remoteBranches NOTIFY headChanged)
    // The stashes' messages, the newest first: stash@{0}, stash@{1}, ...
    Q_PROPERTY(QStringList stashes READ stashes NOTIFY headChanged)
    // The tags the history shows
    Q_PROPERTY(QStringList tags READ tags NOTIFY headChanged)
    // [{name, url}]: the remotes; [{path, branch, current}]: the worktrees
    Q_PROPERTY(QVariantList remotes READ remotes NOTIFY headChanged)
    Q_PROPERTY(QVariantList worktrees READ worktrees NOTIFY headChanged)
    // Whether the work tree has submodules (a .gitmodules)
    Q_PROPERTY(bool hasSubmodules READ hasSubmodules NOTIFY headChanged)
    // Some files are kept in Git LFS (filter=lfs), and whether git-lfs is
    // there to fetch them and to lock
    Q_PROPERTY(bool usesLfs READ usesLfs NOTIFY lfsChanged)
    Q_PROPERTY(bool lfsInstalled READ lfsInstalled NOTIFY lfsChanged)
    // A shallow clone, with only part of the history; a sparse checkout,
    // with only some folders in the working tree
    Q_PROPERTY(bool shallow READ shallow NOTIFY lfsChanged)
    Q_PROPERTY(bool sparse READ sparse NOTIFY lfsChanged)
    // While git bisect looks for the commit that broke something
    Q_PROPERTY(bool bisecting READ bisecting NOTIFY headChanged)
    Q_PROPERTY(QString headSha READ headSha NOTIFY headChanged)
    Q_PROPERTY(QString headMessage READ headMessage NOTIFY headChanged)
    Q_PROPERTY(QString upstream READ upstream NOTIFY headChanged)
    Q_PROPERTY(int ahead READ ahead NOTIFY headChanged)
    Q_PROPERTY(int behind READ behind NOTIFY headChanged)
    Q_PROPERTY(CommitModel *commits READ commits CONSTANT)
    Q_PROPERTY(FileListModel *unstaged READ unstaged CONSTANT)
    Q_PROPERTY(FileListModel *staged READ staged CONSTANT)
    Q_PROPERTY(DiffModel *diff READ diff CONSTANT)
    // Tracked files, as a tree; filled by loadFiles().
    Q_PROPERTY(FileTreeModel *files READ files CONSTANT)
    Q_PROPERTY(DiffSource diffSource READ diffSource NOTIFY diffChanged)
    Q_PROPERTY(QString diffPath READ diffPath NOTIFY diffChanged)
    Q_PROPERTY(QString diffCommit READ diffCommit NOTIFY diffChanged)
    // CompareDiff: what the commit is compared with
    Q_PROPERTY(QString diffBaseName READ diffBaseName NOTIFY diffChanged)
    Q_PROPERTY(QString commitDetails READ commitDetails NOTIFY diffChanged)
    // PatchSetDiff: the patch set the commit on display is compared with.
    Q_PROPERTY(int diffPatchSet READ diffPatchSet NOTIFY diffChanged)
    // What Gerrit has on the commit on display, if it is one of those a
    // push for review sends and Gerrit has a change for it; empty if not.
    // {number, url, patchSet, localPatchSet, unresolved,
    //  messages: [{author, date, text}],
    //  comments: [{patchSet, file, line, author, text, marked}], newest
    //  patch set first; `marked` ones are shown on their lines in the diff,
    //  patchSets: [{number, revision, uploader, date, kind, kindText,
    //  comments, current, local}], newest first}
    Q_PROPERTY(QVariantMap commitReview READ commitReview NOTIFY commitReviewChanged)
    // The inline comments on the lines of the diff on display:
    // {oldSide: {"path:line": [{author, text}]}, newSide: {...}}. Only
    // comments on a patch set that is one side of the diff are there, as
    // line numbers of other patch sets need not match.
    Q_PROPERTY(QVariantMap diffComments READ diffComments NOTIFY commitReviewChanged)
    // Diffs leave out changes of white space (`git diff -w`). Lines of such
    // a diff cannot be staged or folded: their patch would not apply.
    Q_PROPERTY(bool ignoreWhitespace READ ignoreWhitespace WRITE setIgnoreWhitespace
                   NOTIFY ignoreWhitespaceChanged)
    // The file on display has unresolved conflicts; show the merge tool.
    Q_PROPERTY(bool diffConflicted READ diffConflicted NOTIFY diffChanged)
    // The last fold or history edit can be undone.
    Q_PROPERTY(bool canUndoFold READ canUndoFold NOTIFY canUndoFoldChanged)
    // The commit the selected lines most likely belong in (see
    // FoldSuggester), and a sentence explaining it or why there is none.
    Q_PROPERTY(QString suggestedFoldTarget READ suggestedFoldTarget NOTIFY foldSuggestionChanged)
    Q_PROPERTY(QString foldSuggestionText READ foldSuggestionText NOTIFY foldSuggestionChanged)
    // In plan mode write actions are simulated and collected in `plan`;
    // the repository changes only when the plan is executed.
    Q_PROPERTY(bool planning READ planning NOTIFY planningChanged)
    // What the history shows besides local branches.
    Q_PROPERTY(bool showRemotes READ showRemotes WRITE setShowRemotes NOTIFY filterChanged)
    Q_PROPERTY(bool showTags READ showTags WRITE setShowTags NOTIFY filterChanged)
    Q_PROPERTY(bool currentBranchOnly READ currentBranchOnly WRITE setCurrentBranchOnly NOTIFY filterChanged)
    // Where Push goes, or why it can't; see resolvePushTarget().
    Q_PROPERTY(QString pushDescription READ pushDescription NOTIFY headChanged)
    Q_PROPERTY(bool canPush READ canPush NOTIFY headChanged)
    // Gerrit: Push sends HEAD to refs/for/<branch>.
    Q_PROPERTY(bool pushForReview READ pushForReview NOTIFY headChanged)
    // Where the remote's reviews happen: "gerrit", "github", "gitlab", or
    // empty for none.
    Q_PROPERTY(QString reviewHost READ reviewHost NOTIFY headChanged)
    // False when Gerrit already has every commit a push for review would
    // send, so it would only answer "no new changes".
    Q_PROPERTY(bool pushNeeded READ pushNeeded NOTIFY reviewsChanged)
    // What the review host has for those commits or the branch, or why it
    // could not be asked.
    Q_PROPERTY(QString reviewSummary READ reviewSummary NOTIFY reviewsChanged)
    // GitHub, GitLab: the current branch's pull or merge request as the
    // commit model's review role has it on HEAD; empty if there is none.
    Q_PROPERTY(QVariantMap branchReview READ branchReview NOTIFY reviewsChanged)
    // "auto", "plain", "gerrit", "github" or "gitlab"; and the branch
    // Gerrit reviews go to.
    Q_PROPERTY(QString pushMode READ pushMode WRITE setPushMode NOTIFY pushSettingsChanged)
    Q_PROPERTY(QString gerritBranch READ gerritBranch WRITE setGerritBranch NOTIFY pushSettingsChanged)
    // Self-hosted GitHub and GitLab servers, comma-separated, for "auto".
    Q_PROPERTY(QString githubHosts READ githubHosts WRITE setGithubHosts NOTIFY pushSettingsChanged)
    Q_PROPERTY(QString gitlabHosts READ gitlabHosts WRITE setGitlabHosts NOTIFY pushSettingsChanged)
    // The topic the current branch was last pushed for review with.
    Q_PROPERTY(QString reviewTopic READ reviewTopic NOTIFY headChanged)
    // When FETCH_HEAD was last written; invalid if never.
    Q_PROPERTY(QDateTime lastFetch READ lastFetch NOTIFY headChanged)
    // Fetches the upstream's remote in the background this often, without
    // asking for anything; 0 never
    Q_PROPERTY(int autoFetchMinutes READ autoFetchMinutes WRITE setAutoFetchMinutes NOTIFY autoFetchMinutesChanged)
    // "Merging", "Rebasing", ... while git is stopped in such an operation.
    Q_PROPERTY(QString operation READ operation NOTIFY headChanged)
    // Uncommitted changes, drawn as a row on top of the history.
    Q_PROPERTY(bool hasWip READ hasWip NOTIFY headChanged)
    Q_PROPERTY(QVariant wipGraph READ wipGraph NOTIFY headChanged)
    Q_PROPERTY(QString wipLaneColor READ wipLaneColor NOTIFY wipLaneColorChanged)
    // {modified, added, deleted}: files with uncommitted changes.
    Q_PROPERTY(QVariantMap wipCounts READ wipCounts NOTIFY headChanged)
    Q_PROPERTY(PlanModel *plan READ plan CONSTANT)

public:
    // PatchSetDiff: from a Gerrit patch set (diffPatchSet) to the commit
    // on display (diffCommit).
    // CompareDiff: from one commit (diffBaseName) to another (diffCommit),
    // or what the other changed since it split off.
    enum DiffSource { NoDiff, UnstagedDiff, StagedDiff, CommitDiff, FileContent, PatchSetDiff, CompareDiff };
    Q_ENUM(DiffSource)

    explicit Repository(QObject *parent = nullptr);
    ~Repository() override;

    QString path() const { return m_git.workDir(); }
    bool valid() const { return !m_git.workDir().isEmpty(); }
    bool busy() const { return m_pendingJobs > 0; }
    bool writing() const { return m_pendingWrites > 0; }
    QString branch() const { return m_status.branch; }
    QStringList branches() const { return m_branches; }
    QStringList remoteBranches() const { return m_remoteBranches; }
    QStringList stashes() const { return m_stashes; }
    QStringList tags() const { return m_tags; }
    QVariantList remotes() const { return m_remoteList; }
    QVariantList worktrees() const { return m_worktreeList; }
    bool hasSubmodules() const;
    bool usesLfs() const { return m_usesLfs; }
    bool lfsInstalled() const { return m_lfsInstalled; }
    bool shallow() const { return m_shallow; }
    bool sparse() const { return m_sparse; }
    bool bisecting() const;
    QString headSha() const { return m_status.headSha; }
    QString headMessage() const { return m_headMessage; }
    QString upstream() const { return m_status.upstream; }
    int ahead() const { return m_status.ahead; }
    int behind() const { return m_status.behind; }
    CommitModel *commits() { return &m_commits; }
    FileListModel *unstaged() { return &m_unstaged; }
    FileListModel *staged() { return &m_staged; }
    DiffModel *diff() { return &m_diff; }
    FileTreeModel *files() { return &m_files; }
    DiffSource diffSource() const { return m_diffSource; }
    QString diffPath() const { return m_diffPath; }
    QString diffCommit() const { return m_diffCommit; }
    QString commitDetails() const { return m_commitDetails; }
    int diffPatchSet() const { return m_diffSource == PatchSetDiff ? m_diffPatchSet : 0; }
    QString diffBaseName() const { return m_diffSource == CompareDiff ? m_diffBaseName : QString(); }
    QVariantMap commitReview() const;
    QVariantMap diffComments() const;
    bool diffConflicted() const { return m_diffConflicted; }
    bool ignoreWhitespace() const { return m_ignoreWhitespace; }
    void setIgnoreWhitespace(bool ignore);
    bool canUndoFold() const { return !m_lastFold.newHead.isEmpty(); }
    QString suggestedFoldTarget() const { return m_suggestedFoldTarget; }
    QString foldSuggestionText() const { return m_foldSuggestionText; }
    bool planning() const { return m_plan != nullptr; }
    bool showRemotes() const { return m_filter.remotes; }
    void setShowRemotes(bool show);
    bool showTags() const { return m_filter.tags; }
    void setShowTags(bool show);
    bool currentBranchOnly() const { return m_filter.currentBranchOnly; }
    void setCurrentBranchOnly(bool only);
    QString pushDescription() const { return m_pushTarget.description(); }
    bool canPush() const { return m_pushTarget.error.isEmpty() && !m_pushTarget.args.isEmpty(); }
    bool pushForReview() const { return m_pushTarget.forReview; }
    QString reviewHost() const;
    bool pushNeeded() const;
    QString reviewSummary() const;
    QVariantMap branchReview() const;
    QString pushMode() const;
    void setPushMode(const QString &mode);
    QString gerritBranch() const { return m_pushSettings.gerritBranch; }
    void setGerritBranch(const QString &branch);
    QString githubHosts() const { return m_pushSettings.githubHosts.join(QStringLiteral(", ")); }
    void setGithubHosts(const QString &hosts);
    QString gitlabHosts() const { return m_pushSettings.gitlabHosts.join(QStringLiteral(", ")); }
    void setGitlabHosts(const QString &hosts);
    QString reviewTopic() const { return m_pushTarget.savedTopic; }
    QDateTime lastFetch() const;
    int autoFetchMinutes() const { return m_autoFetchMinutes; }
    void setAutoFetchMinutes(int minutes);
    // A background fetch, if the last fetch is older than the interval
    // and nothing else is going on; what the timer runs
    Q_INVOKABLE void fetchInBackground();
    QString operation() const;
    bool hasWip() const { return m_historyWip; }
    QVariant wipGraph() const { return QVariant::fromValue(m_wipGraph); }
    QString wipLaneColor() const { return laneColorName(m_wipGraph.color); }
    QVariantMap wipCounts() const;
    PlanModel *plan() { return &m_planModel; }

    // Accepts a local path or a file:// URL anywhere inside a work tree.
    Q_INVOKABLE void open(const QString &pathOrUrl);
    // Whether a folder is still there, for the repositories opened before
    Q_INVOKABLE bool folderExists(const QString &path) const;
    // The folder as the file system names it, through symbolic links (on
    // macOS, /var is /private/var); as given if it is gone
    Q_INVOKABLE QString canonicalPath(const QString &path) const;
    // A path to show: in the home folder, from ~
    Q_INVOKABLE QString displayPath(const QString &path) const;
    // Reloads refs, history and status.
    Q_INVOKABLE void refresh();
    // Reloads status and the file diff on display; reloads history only if
    // HEAD moved.
    Q_INVOKABLE void refreshStatus();

    Q_INVOKABLE void showCommit(const QString &sha);
    // Compares the commit on display with its change's patch set `number`,
    // fetching the patch set first if it is not here yet.
    Q_INVOKABLE void comparePatchSet(int number);
    Q_INVOKABLE void showFile(const QString &path, bool staged);
    // Shows a file's content as it is in the working tree.
    Q_INVOKABLE void showFileContent(const QString &path);
    // Shows the first changed file, unstaged ones first.
    Q_INVOKABLE void showWorkingTree();
    // Lists the tracked files (as the index has them) into `files`.
    Q_INVOKABLE void loadFiles();
    // Asks for the files and lines a commit changes; they arrive in the
    // commit model's stats role. Requests are batched.
    Q_INVOKABLE void requestStats(const QString &sha);

    Q_INVOKABLE void stageFile(const QString &path);
    Q_INVOKABLE void unstageFile(const QString &path);
    Q_INVOKABLE void stageFiles(const QStringList &paths);
    Q_INVOKABLE void unstageFiles(const QStringList &paths);
    Q_INVOKABLE void stageAll();
    Q_INVOKABLE void unstageAll();
    // Stages the selected lines of the unstaged diff, or unstages the
    // selected lines of the staged diff.
    Q_INVOKABLE void applySelection();
    // Like applySelection(), for the hunk containing `row` only.
    Q_INVOKABLE void applyHunk(int row);
    // Like applySelection() for the line at `row`, or for the whole
    // selection if `row` is part of it.
    Q_INVOKABLE void applyLine(int row);

    // Writes `content` to the conflicted file and stages it.
    Q_INVOKABLE void resolveConflict(const QString &path, const QString &content);
    // Keeps one side's version of a conflicted file as a whole, or deletes
    // the file if that side deleted it.
    Q_INVOKABLE void resolveWithSide(const QString &path, bool theirs);

    // Fetches the upstream's remote, or all remotes.
    Q_INVOKABLE void fetch();

    // Branches: what is wrong with a name for a new one, as git's
    // check-ref-format sees it or because a branch has it already; "" if
    // nothing is. Making one at `start` (a commit or ref), and switching to
    // it; switching to a branch, a remote-tracking one making a local branch
    // that tracks it; renaming and deleting one, `force` deleting one not
    // merged. Off while planning. Switching keeps uncommitted changes where
    // git can, and else says which files are in the way.
    Q_INVOKABLE QString branchNameError(const QString &name) const;
    Q_INVOKABLE void createBranch(const QString &name, const QString &start, bool checkout);
    Q_INVOKABLE void checkoutBranch(const QString &name);
    // A commit or tag checked out on no branch (detached HEAD), to look at
    // or build an old version
    Q_INVOKABLE void checkoutDetached(const QString &rev);
    Q_INVOKABLE void renameBranch(const QString &from, const QString &to);
    Q_INVOKABLE void deleteBranch(const QString &name, bool force);

    // Others' changes: pulling fetches the upstream's remote and merges the
    // upstream, or rebases onto it as pull.rebase says; merging a branch
    // into the current one, rebasing the current one onto a branch. One
    // that stops at conflicts leaves them to resolve in the merge tool, and
    // the operation (operation) to continue or abort. Off while planning.
    // `how`: as configured if empty, else "merge", "rebase" or "ff-only";
    // `autostash` puts the changes aside meanwhile
    Q_INVOKABLE void pull(const QString &how = {}, bool autostash = false);
    // `how`: as configured if empty, else "no-ff" (always a merge commit),
    // "ff-only", or "squash", which stages what the branch changed for a
    // commit of its own
    Q_INVOKABLE void mergeBranch(const QString &name, const QString &how = {});
    // `after` given: only the current branch's commits after it move onto
    // `name` (git rebase --onto name after)
    Q_INVOKABLE void rebaseOnto(const QString &name, const QString &after = {});
    Q_INVOKABLE void continueOperation();
    Q_INVOKABLE void abortOperation();

    // Changes thrown away, which nothing brings back: a file's unstaged
    // changes, an untracked file deleted; the lines selected in the
    // unstaged diff. Off while planning.
    Q_INVOKABLE void discardFile(const QString &path);
    Q_INVOKABLE void discardFiles(const QStringList &paths);
    // What `git clean` would delete: untracked files and folders, ignored
    // ones too with `ignored`; cleanPreviewed() with them
    Q_INVOKABLE void previewClean(bool ignored);
    // Deletes those, and only those
    Q_INVOKABLE void clean(const QStringList &paths, bool ignored);
    // Patterns that ignore an untracked path, most specific first: it
    // alone, the files with its extension, its folder, its name anywhere
    Q_INVOKABLE QStringList ignorePatterns(const QString &path) const;
    // Adds `pattern` to the top folder's .gitignore, shared with everyone,
    // or with `local` to this clone's info/exclude
    Q_INVOKABLE void ignore(const QString &pattern, bool local);
    Q_INVOKABLE void discardSelection();

    // Stashes: the uncommitted changes put away, untracked files with
    // them if asked, under a message; one applied again, and dropped with
    // it if `pop` (not when it stops at conflicts); one dropped. `index`
    // is the n of stash@{n}. Off while planning.
    // Only the staged changes with `stagedOnly`, or only the `paths`'.
    Q_INVOKABLE void stash(const QString &message, bool includeUntracked, bool stagedOnly = false,
                           const QStringList &paths = {});
    Q_INVOKABLE void applyStash(int index, bool pop);
    Q_INVOKABLE void dropStash(int index);
    // Where HEAD was and what nothing refers to any more: lostWorkFound()
    // with {sha, subject, time, kind ("reflog" or "dangling"), selector,
    // action, unreachable, stash}, newest first in each kind
    Q_INVOKABLE void findLostWork();
    // Locks a file kept in Git LFS for others, or unlocks it; `force`
    // unlocks someone else's lock
    Q_INVOKABLE void lockFile(const QString &path, bool lock, bool force = false);
    // A dropped stash's commit back into the stash list, as stash@{0}
    Q_INVOKABLE void restoreStash(const QString &sha, const QString &subject);

    // Commits of other places: one picked onto the current branch, one
    // undone by a new commit; a merge commit by its first parent's side.
    // Stopped at conflicts, they wait as merges do (continueOperation()).
    // The current branch reset to a commit: `mode` soft keeps what lies
    // between staged, mixed unstaged, hard throws it and every
    // uncommitted change away. Off while planning.
    Q_INVOKABLE void cherryPick(const QString &sha);
    // Several, in the order given (oldest first); those the branch has, or
    // whose changes it has already, are left out. No merges.
    Q_INVOKABLE void cherryPickCommits(const QStringList &shas);
    Q_INVOKABLE void revertCommit(const QString &sha);
    Q_INVOKABLE void resetTo(const QString &sha, const QString &mode);

    // Two commits compared: from `base` to `commit`, or with `threeDot`
    // what `commit` changed since it split off from `base`; `baseName`
    // says what `base` is.
    Q_INVOKABLE void compareCommits(const QString &base, const QString &commit, const QString &baseName,
                                    bool threeDot);

    // Tags: what is wrong with a name for a new one; one made at a commit,
    // annotated with `message` if there is one; one deleted; one pushed to
    // the upstream's remote (origin without one). Off while planning.
    Q_INVOKABLE QString tagNameError(const QString &name) const;
    Q_INVOKABLE void createTag(const QString &name, const QString &sha, const QString &message);
    Q_INVOKABLE void deleteTag(const QString &name);
    Q_INVOKABLE void pushTag(const QString &name);
    // Every tag to `remote`, the upstream's if empty
    Q_INVOKABLE void pushAllTags(const QString &remote = {});
    // A local branch to `remote` as `remoteBranch`, tracked from then on
    // with `track`
    Q_INVOKABLE void pushBranch(const QString &branch, const QString &remote, const QString &remoteBranch,
                                bool track);
    // A remote-tracking branch, origin/x, deleted on its remote
    Q_INVOKABLE void deleteRemoteBranch(const QString &name);

    // A repository to work in: cloned from `url` into a folder of its name
    // in `parent`, or a new one made in `folder`; opened once it is there.
    // For a big one: `depth` > 0 only the last that many commits (a
    // shallow clone), `blobless` file contents fetched when needed
    // (--filter=blob:none).
    Q_INVOKABLE void cloneRepository(const QString &url, const QString &parent, int depth = 0, bool blobless = false);
    // A shallow clone: more of the history, `commits` more, or all of it
    // with 0
    Q_INVOKABLE void fetchMoreHistory(int commits);
    // Sparse checkout (cone mode): only these folders in the working tree,
    // besides the files at the top; or all of it again
    Q_INVOKABLE void setSparseFolders(const QStringList &folders);
    Q_INVOKABLE void disableSparseCheckout();
    // sparseFoldersLoaded() with the folders a sparse checkout has now
    Q_INVOKABLE void loadSparseFolders();
    Q_INVOKABLE void initRepository(const QString &folder);
    // The folder a clone of `url` goes into: its last part, without .git
    Q_INVOKABLE QString cloneFolderName(const QString &url) const;

    // Remotes: added, renamed, pointed elsewhere, removed; a
    // remote-tracking branch made the current branch's upstream. Off while
    // planning.
    Q_INVOKABLE void addRemote(const QString &name, const QString &url);
    Q_INVOKABLE void renameRemote(const QString &from, const QString &to);
    Q_INVOKABLE void setRemoteUrl(const QString &name, const QString &url);
    Q_INVOKABLE void removeRemote(const QString &name);
    Q_INVOKABLE void setUpstream(const QString &remoteBranch);

    // Worktrees: another work tree of this repository at `path`, on
    // `branch`, made from `start` when `create`; one removed (not the one
    // open). Submodules brought to the commits recorded, cloned if new.
    Q_INVOKABLE void addWorktree(const QString &path, const QString &branch, bool create, const QString &start);
    Q_INVOKABLE void removeWorktree(const QString &path);
    Q_INVOKABLE void updateSubmodules();

    // Patches: a commit saved to a file as git format-patch writes it; one
    // applied, as a commit with its message (git am) when it is such a
    // file, else to the working tree (git apply).
    Q_INVOKABLE void savePatch(const QString &sha, const QString &file);
    Q_INVOKABLE void applyPatch(const QString &file);

    // Bisecting: a commit marked "good" or "bad", starting a bisection if
    // there is none; the commit checked out marked "good", "bad" or
    // "skip"; the bisection ended, back where it began. What git found is
    // its notice. Off while planning.
    Q_INVOKABLE void bisectMark(const QString &sha, const QString &verdict);
    Q_INVOKABLE void bisectCurrent(const QString &verdict);
    Q_INVOKABLE void bisectReset();
    // Pushes as pushDescription says. A rejected push emits pushRejected()
    // so the UI can offer `force`, which uses --force-with-lease.
    // `review` holds options for the review host: {topic, state ("wip",
    // "ready" or empty), reviewers, hashtags} for Gerrit; {create, target,
    // title, state, reviewers, labels, removeSourceBranch} for GitHub and
    // GitLab. Lists are comma-separated strings.
    Q_INVOKABLE void push(bool force = false, const QVariantMap &review = {});
    // What a push with `review` would send: Gerrit's refspec, or the
    // commands for GitHub and GitLab; or why it cannot. {text, error}.
    Q_INVOKABLE QVariantMap pushPreview(const QVariantMap &review) const;

    // Emits committed() on success. `skipHooks`: without the pre-commit
    // and commit-msg hooks (--no-verify); `resetAuthor`: an amend made
    // the user's, now (--reset-author).
    Q_INVOKABLE void commit(const QString &message, bool amend, bool skipHooks = false, bool resetAuthor = false);
    // The message commit.template starts a commit with, its comments left
    // out; empty without one
    Q_PROPERTY(QString commitTemplate READ commitTemplate NOTIFY commitTemplateChanged)
    QString commitTemplate() const { return m_commitTemplate; }
    // Commits `patch` on HEAD alone, apart from the index (see commitPatch()
    // in PatchCommit.h); while planning, plans it. Emits patchCommitted().
    Q_INVOKABLE void commitPatch(const QByteArray &patch, const QString &message);

    // Moves `patch` from `from` to `to`, each a commit of the branch or
    // empty for the working tree (see moveChange() in ChangeMove.h); the
    // index of its paths follows. While planning, plans it. Emits
    // changeMoved(); undoFold() takes it back.
    Q_INVOKABLE void moveChange(const QString &from, const QString &to, const QByteArray &patch,
                                const QString &title);

    // Why the selected lines cannot be folded into `sha`, or an empty
    // string if they can.
    Q_INVOKABLE QString foldBlocker(const QString &sha) const;
    // What keeps anything from folding into `sha`, whatever is selected:
    // for a whole file, whose lines are selected once it is shown.
    Q_INVOKABLE QString foldTargetBlocker(const QString &sha) const;
    // Whether a remote-tracking branch contains `sha` already, so folding
    // into it means force pushing.
    Q_INVOKABLE bool isPublished(const QString &sha) const;
    // Folds the selected lines of the working diff into `sha` and rewrites
    // the commits after it.
    Q_INVOKABLE void foldSelection(const QString &sha);
    // Restores the branch and index as they were before the last fold or
    // history edit, as long as HEAD has not moved since.
    Q_INVOKABLE void undoFold();

    // History edits, see HistoryEditor. `kind` is "reword", "moveUp",
    // "moveDown", "move" (to where `to` is), "squash" (into the commit
    // below), "fixup" (likewise, keeping that commit's message) or "drop".
    // Why the edit is not possible, or an empty string if it is.
    Q_INVOKABLE QString editBlocker(const QString &kind, const QString &sha, const QString &to = {}) const;
    // Whether the edit rewrites a commit a remote-tracking branch has.
    Q_INVOKABLE bool editRewritesPublished(const QString &kind, const QString &sha, const QString &to = {}) const;
    // `message` is the new one, for "reword".
    Q_INVOKABLE void editCommit(const QString &kind, const QString &sha, const QString &to = {},
                                const QString &message = {});
    // Loads a commit's full message; it arrives with messageLoaded().
    Q_INVOKABLE void requestMessage(const QString &sha);

    Q_INVOKABLE void startPlanning();
    Q_INVOKABLE void discardPlan();
    Q_INVOKABLE void executePlan();
    Q_INVOKABLE void removePlanEntry(int entryId);

signals:
    void wipLaneColorChanged();
    void pathChanged();
    void busyChanged();
    void headChanged();
    void diffChanged();
    void ignoreWhitespaceChanged();
    void canUndoFoldChanged();
    void foldSuggestionChanged();
    void planningChanged();
    void filterChanged();
    void pushSettingsChanged();
    void pushRejected(const QString &reason, const QString &target);
    void reviewsChanged();
    void commitReviewChanged();
    void committed();
    // After commitPatch(), whether it went through; error says why not.
    void patchCommitted(bool ok, const QString &error);
    // After moveChange(), whether it went through; error says why not.
    void changeMoved(bool ok, const QString &error);
    void messageLoaded(const QString &sha, const QString &message);
    void lostWorkFound(const QVariantList &commits);
    void cleanPreviewed(const QStringList &paths, bool ignored);
    void lfsChanged();
    void commitTemplateChanged();
    void autoFetchMinutesChanged();
    void sparseFoldersLoaded(const QStringList &folders);
    void errorOccurred(const QString &message);
    // `links` are URLs the remote printed, such as Gerrit's changes.
    void notice(const QString &message, const QStringList &links);

private:
    struct LastFold
    {
        QString oldHead;
        QString newHead;
        QByteArray stagedPatch; // applied to the index by the fold
        bool edit = false;      // a history edit, not a fold
        bool indexCarried = false; // the edit moved the index along with HEAD
    };

    struct PlanSnapshot
    {
        QList<PlanEntry> entries;
        bool executable = false;
        QString summary;
    };

    // What a job hands back to the UI thread.
    struct Outcome
    {
        QString workDir; // the repository the job ran in
        int planGeneration = 0;
        std::optional<PlanSnapshot> plan;
        bool planEnded = false;
        QString openedPath;
        RepositoryState state;
        std::optional<LastFold> fold;
        bool committed = false;
        std::optional<QString> patchCommitted; // empty: it went through
        std::optional<QString> changeMoved;    // likewise
        std::optional<std::pair<QString, QString>> message; // sha and message
        QString gitDir;
        QString pushRejected; // why the remote refused a push
        QHash<QString, CommitStats> stats;
        std::optional<QStringList> trackedFiles;
        std::optional<QVariantList> lostWork;
        std::optional<std::pair<QStringList, bool>> cleanPreview;
        std::optional<std::pair<bool, bool>> lfs; // used, installed
        std::optional<QString> commitTemplate;
        std::optional<std::pair<bool, bool>> layout; // shallow, sparse
        std::optional<QStringList> sparseFolders;
        std::shared_ptr<FileTreeNode> fileTree;
        QString notice;
        QStringList links;
        QString error;
    };
    using Job = std::function<Outcome(const GitRunner &)>;
    enum class JobKind { Read, Write };

    void enqueue(JobKind kind, Job job);
    void finish(JobKind kind, Outcome outcome);
    void applyState(const RepositoryState &state);
    // Runs `args`, then reloads the state.
    void enqueueCommand(const QString &what, const QStringList &args, const QByteArray &input = {});
    // A write by git, then the state again, the history in full: `done` on
    // success, else `failed` with git's reason. Refused while planning.
    // A merge, rebase, cherry-pick or revert that stops at conflicts says
    // so, and waits for them.
    void enqueueGitWrite(const QStringList &args, const QString &done, const QString &failed);
    // The same for a command that talks to a remote
    void enqueueNetwork(const QStringList &args, const QString &done, const QString &failed);
    // The remote of a remote-tracking ref, origin/x: the known remote with
    // the longest name it starts with, as names may hold a /
    QString remoteOf(const QString &ref) const;
    void enqueuePlanEntry(const PlanEntry &entry);
    void endPlan();
    void scheduleReviewQuery();
    void queryReviewsNow();
    void setReviews(const ReviewQuery &reviews);
    static PlanSnapshot snapshot(const PlanSession &plan);
    // Where jobs read from: the plan's simulation while planning.
    static StateSource sourceFor(const GitRunner &git, const std::shared_ptr<PlanSession> &plan);

    void setDiffView(DiffSource source, const QString &path, const QString &commit, int patchSet = 0);
    // What Gerrit has on the commit on display, if anything.
    const ReviewInfo *shownReview() const;
    // Loads the diff on display again, as it is now.
    void reloadDiff();
    DiffRequest diffRequest() const;
    HistoryRequest historyRequest(bool force) const;
    // For another repository: all of it read, nothing kept from this one
    HistoryRequest freshHistoryRequest() const;
    void loadRequestedStats();
    void forgetFold();
    void updateFoldSuggestion();
    QString commitLabel(const QString &sha) const;
    bool isMergeCommit(const QString &sha) const;
    // The oldest commit `kind` rewrites, or an empty string if it can't.
    QString oldestEdited(const QString &kind, const QString &sha, const QString &to) const;
    QByteArray selectionPatch(PatchDirection direction) const;

    GitRunner m_git;
    QThreadPool m_pool;
    // Asking the review host takes a network round trip or more; it gets a
    // thread of its own so it never holds up the git queue.
    QThreadPool m_reviewPool;
    // `git log -S` reads every commit's changes, which can take a while;
    // it gets a thread of its own too
    QThreadPool m_searchPool;
    bool m_usesLfs = false;
    bool m_lfsInstalled = false;
    QString m_commitTemplate;
    bool m_shallow = false;
    bool m_sparse = false;
    // What the repository is set up with: Git LFS, commit.template
    void checkSetup();
    void searchCode(const QStringList &texts, bool caseSensitive, bool regex);
    QTimer m_reviewTimer;
    int m_reviewGeneration = 0;
    ReviewQuery m_reviews;
    int m_pendingJobs = 0;
    int m_pendingWrites = 0;
    // Bumped whenever the diff view changes, so stale results are dropped,
    // and queued loads for a view the user already left are skipped.
    std::shared_ptr<std::atomic_int> m_diffGeneration = std::make_shared<std::atomic_int>(0);

    StatusInfo m_status;
    QStringList m_branches;
    QStringList m_remoteBranches;
    QStringList m_tags;
    QStringList m_stashes;
    // The commits on display and their graph, and what they were read for,
    // to keep when nothing they depend on moved (HistoryRequest)
    QList<CommitInfo> m_shownCommits;
    QList<GraphRow> m_shownGraph;
    std::optional<HistoryFilter> m_shownFilter;
    QStringList m_shownTargets;
    QSet<QString> m_shownShas;
    // As QML reads them, made once per reading of the history
    QVariantList m_remoteList;
    QVariantList m_worktreeList;
    QString m_historyHead;
    bool m_historyWip = false;
    GraphRow m_wipGraph;
    HistoryFilter m_filter;
    PushSettings m_pushSettings;
    PushTarget m_pushTarget;
    QString m_gitDir;
    QStringList m_statsWanted;
    QSet<QString> m_statsRequested;
    QTimer m_statsTimer;
    QTimer m_autoFetchTimer;
    int m_autoFetchMinutes = 0;
    QString m_headMessage;
    QSet<QString> m_foldTargets;
    QStringList m_editChain; // the fold targets in order, HEAD first
    QSet<QString> m_unpublished;
    CommitModel m_commits;
    FileListModel m_unstaged{false};
    FileListModel m_staged{true};
    DiffModel m_diff;
    FileTreeModel m_files;
    DiffSource m_diffSource = NoDiff;
    QString m_diffPath;
    QString m_diffCommit;
    int m_diffPatchSet = 0;
    QString m_diffBase; // PatchSetDiff: the patch set's revision; CompareDiff: the other commit
    QString m_diffBaseName; // CompareDiff
    bool m_diffThreeDot = false; // CompareDiff
    QString m_commitDetails;
    bool m_diffConflicted = false;
    bool m_ignoreWhitespace = false;
    FoldSuggester m_suggester;
    QString m_suggestedFoldTarget;
    QString m_foldSuggestionText;
    LastFold m_lastFold;

    std::shared_ptr<PlanSession> m_plan; // used on the git thread only
    // Bumped whenever planning starts or ends, so results computed for the
    // other mode are dropped.
    int m_planGeneration = 0;
    PlanModel m_planModel;
};
