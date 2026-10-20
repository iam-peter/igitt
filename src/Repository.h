#pragma once

#include "core/FoldSuggester.h"
#include "core/GitRunner.h"
#include "core/PlanSession.h"
#include "core/StateLoader.h"
#include "models/CommitModel.h"
#include "models/DiffModel.h"
#include "models/FileListModel.h"
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
    Q_PROPERTY(QString headSha READ headSha NOTIFY headChanged)
    Q_PROPERTY(QString headMessage READ headMessage NOTIFY headChanged)
    Q_PROPERTY(QString upstream READ upstream NOTIFY headChanged)
    Q_PROPERTY(int ahead READ ahead NOTIFY headChanged)
    Q_PROPERTY(int behind READ behind NOTIFY headChanged)
    Q_PROPERTY(CommitModel *commits READ commits CONSTANT)
    Q_PROPERTY(FileListModel *unstaged READ unstaged CONSTANT)
    Q_PROPERTY(FileListModel *staged READ staged CONSTANT)
    Q_PROPERTY(DiffModel *diff READ diff CONSTANT)
    Q_PROPERTY(DiffSource diffSource READ diffSource NOTIFY diffChanged)
    Q_PROPERTY(QString diffPath READ diffPath NOTIFY diffChanged)
    Q_PROPERTY(QString diffCommit READ diffCommit NOTIFY diffChanged)
    Q_PROPERTY(QString commitDetails READ commitDetails NOTIFY diffChanged)
    // The file on display has unresolved conflicts; show the merge tool.
    Q_PROPERTY(bool diffConflicted READ diffConflicted NOTIFY diffChanged)
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
    Q_PROPERTY(bool pushForReview READ pushForReview NOTIFY headChanged)
    // "auto", "plain" or "gerrit"; and the branch Gerrit reviews go to.
    Q_PROPERTY(QString pushMode READ pushMode WRITE setPushMode NOTIFY pushSettingsChanged)
    Q_PROPERTY(QString gerritBranch READ gerritBranch WRITE setGerritBranch NOTIFY pushSettingsChanged)
    // When FETCH_HEAD was last written; invalid if never.
    Q_PROPERTY(QDateTime lastFetch READ lastFetch NOTIFY headChanged)
    // "Merging", "Rebasing", ... while git is stopped in such an operation.
    Q_PROPERTY(QString operation READ operation NOTIFY headChanged)
    // Uncommitted changes, drawn as a row on top of the history.
    Q_PROPERTY(bool hasWip READ hasWip NOTIFY headChanged)
    Q_PROPERTY(QVariant wipGraph READ wipGraph NOTIFY headChanged)
    Q_PROPERTY(QString wipLaneColor READ wipLaneColor NOTIFY headChanged)
    // {modified, added, deleted}: files with uncommitted changes.
    Q_PROPERTY(QVariantMap wipCounts READ wipCounts NOTIFY headChanged)
    Q_PROPERTY(PlanModel *plan READ plan CONSTANT)

public:
    enum DiffSource { NoDiff, UnstagedDiff, StagedDiff, CommitDiff };
    Q_ENUM(DiffSource)

    explicit Repository(QObject *parent = nullptr);
    ~Repository() override;

    QString path() const { return m_git.workDir(); }
    bool valid() const { return !m_git.workDir().isEmpty(); }
    bool busy() const { return m_pendingJobs > 0; }
    bool writing() const { return m_pendingWrites > 0; }
    QString branch() const { return m_status.branch; }
    QString headSha() const { return m_status.headSha; }
    QString headMessage() const { return m_headMessage; }
    QString upstream() const { return m_status.upstream; }
    int ahead() const { return m_status.ahead; }
    int behind() const { return m_status.behind; }
    CommitModel *commits() { return &m_commits; }
    FileListModel *unstaged() { return &m_unstaged; }
    FileListModel *staged() { return &m_staged; }
    DiffModel *diff() { return &m_diff; }
    DiffSource diffSource() const { return m_diffSource; }
    QString diffPath() const { return m_diffPath; }
    QString diffCommit() const { return m_diffCommit; }
    QString commitDetails() const { return m_commitDetails; }
    bool diffConflicted() const { return m_diffConflicted; }
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
    bool pushForReview() const { return m_pushTarget.gerrit; }
    QString pushMode() const;
    void setPushMode(const QString &mode);
    QString gerritBranch() const { return m_pushSettings.gerritBranch; }
    void setGerritBranch(const QString &branch);
    QDateTime lastFetch() const;
    QString operation() const;
    bool hasWip() const { return m_historyWip; }
    QVariant wipGraph() const { return QVariant::fromValue(m_wipGraph); }
    QString wipLaneColor() const { return laneColorName(m_wipGraph.color); }
    QVariantMap wipCounts() const;
    PlanModel *plan() { return &m_planModel; }

    // Accepts a local path or a file:// URL anywhere inside a work tree.
    Q_INVOKABLE void open(const QString &pathOrUrl);
    // Reloads refs, history and status.
    Q_INVOKABLE void refresh();
    // Reloads status and the file diff on display; reloads history only if
    // HEAD moved.
    Q_INVOKABLE void refreshStatus();

    Q_INVOKABLE void showCommit(const QString &sha);
    Q_INVOKABLE void showFile(const QString &path, bool staged);
    // Shows the first changed file, unstaged ones first.
    Q_INVOKABLE void showWorkingTree();
    // Asks for the files and lines a commit changes; they arrive in the
    // commit model's stats role. Requests are batched.
    Q_INVOKABLE void requestStats(const QString &sha);

    Q_INVOKABLE void stageFile(const QString &path);
    Q_INVOKABLE void unstageFile(const QString &path);
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
    // Pushes as pushDescription says. A rejected push emits pushRejected()
    // so the UI can offer `force`, which uses --force-with-lease.
    Q_INVOKABLE void push(bool force = false);

    // Emits committed() on success.
    Q_INVOKABLE void commit(const QString &message, bool amend);

    // Why the selected lines cannot be folded into `sha`, or an empty
    // string if they can.
    Q_INVOKABLE QString foldBlocker(const QString &sha) const;
    // Whether a remote-tracking branch contains `sha` already, so folding
    // into it means force pushing.
    Q_INVOKABLE bool isPublished(const QString &sha) const;
    // Folds the selected lines of the working diff into `sha` and rewrites
    // the commits after it.
    Q_INVOKABLE void foldSelection(const QString &sha);
    // Restores the branch and index as they were before the last fold, as
    // long as HEAD has not moved since.
    Q_INVOKABLE void undoFold();

    Q_INVOKABLE void startPlanning();
    Q_INVOKABLE void discardPlan();
    Q_INVOKABLE void executePlan();
    Q_INVOKABLE void removePlanEntry(int entryId);

signals:
    void pathChanged();
    void busyChanged();
    void headChanged();
    void diffChanged();
    void canUndoFoldChanged();
    void foldSuggestionChanged();
    void planningChanged();
    void filterChanged();
    void pushSettingsChanged();
    void pushRejected(const QString &reason, const QString &target);
    void committed();
    void errorOccurred(const QString &message);
    void notice(const QString &message);

private:
    struct LastFold
    {
        QString oldHead;
        QString newHead;
        QByteArray stagedPatch; // applied to the index by the fold
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
        QString gitDir;
        QString pushRejected; // why the remote refused a push
        QHash<QString, CommitStats> stats;
        QString notice;
        QString error;
    };
    using Job = std::function<Outcome(const GitRunner &)>;
    enum class JobKind { Read, Write };

    void enqueue(JobKind kind, Job job);
    void finish(JobKind kind, Outcome outcome);
    void applyState(const RepositoryState &state);
    // Runs `args`, then reloads the state.
    void enqueueCommand(const QString &what, const QStringList &args, const QByteArray &input = {});
    void enqueuePlanEntry(const PlanEntry &entry);
    void endPlan();
    static PlanSnapshot snapshot(const PlanSession &plan);
    // Where jobs read from: the plan's simulation while planning.
    static StateSource sourceFor(const GitRunner &git, const std::shared_ptr<PlanSession> &plan);

    void setDiffView(DiffSource source, const QString &path, const QString &commit);
    DiffRequest diffRequest() const;
    HistoryRequest historyRequest(bool force) const;
    void loadRequestedStats();
    void forgetFold();
    void updateFoldSuggestion();
    QString commitLabel(const QString &sha) const;
    QByteArray selectionPatch(PatchDirection direction) const;

    GitRunner m_git;
    QThreadPool m_pool;
    int m_pendingJobs = 0;
    int m_pendingWrites = 0;
    // Bumped whenever the diff view changes, so stale results are dropped,
    // and queued loads for a view the user already left are skipped.
    std::shared_ptr<std::atomic_int> m_diffGeneration = std::make_shared<std::atomic_int>(0);

    StatusInfo m_status;
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
    QString m_headMessage;
    QSet<QString> m_foldTargets;
    QSet<QString> m_unpublished;
    CommitModel m_commits;
    FileListModel m_unstaged{false};
    FileListModel m_staged{true};
    DiffModel m_diff;
    DiffSource m_diffSource = NoDiff;
    QString m_diffPath;
    QString m_diffCommit;
    QString m_commitDetails;
    bool m_diffConflicted = false;
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
