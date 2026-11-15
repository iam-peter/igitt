#include "Repository.h"

#include "core/ChangeMove.h"
#include "core/FoldEngine.h"
#include "core/GerritReview.h"
#include "core/HistoryEditor.h"
#include "core/LaneColors.h"
#include "core/LostWork.h"
#include "core/MergeConflict.h"
#include "core/PatchCommit.h"
#include "core/PatchBuilder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QUrl>

namespace {

DiffKind diffKind(Repository::DiffSource source)
{
    switch (source) {
    case Repository::UnstagedDiff: return DiffKind::Unstaged;
    case Repository::StagedDiff: return DiffKind::Staged;
    case Repository::CommitDiff: return DiffKind::Commit;
    case Repository::FileContent: return DiffKind::Content;
    case Repository::PatchSetDiff: return DiffKind::Range;
    case Repository::CompareDiff: return DiffKind::Range;
    case Repository::NoDiff: break;
    }
    return DiffKind::None;
}

} // namespace

Repository::Repository(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_reviewPool.setMaxThreadCount(1);
    m_searchPool.setMaxThreadCount(1);
    connect(&m_commits, &CommitModel::codeSearchRequested, this, &Repository::searchCode);
    m_reviewTimer.setSingleShot(true);
    m_reviewTimer.setInterval(400);
    connect(&m_reviewTimer, &QTimer::timeout, this, &Repository::queryReviewsNow);
    connect(&m_diff, &DiffModel::selectionChanged, this, &Repository::updateFoldSuggestion);
    connect(this, &Repository::headChanged, this, &Repository::wipLaneColorChanged);
    connect(LaneColors::instance(), &LaneColors::changed, this, &Repository::wipLaneColorChanged);
    // Rows request their stats as they scroll into view; collect a batch.
    m_statsTimer.setSingleShot(true);
    m_statsTimer.setInterval(30);
    connect(&m_statsTimer, &QTimer::timeout, this, &Repository::loadRequestedStats);
    connect(&m_autoFetchTimer, &QTimer::timeout, this, &Repository::fetchInBackground);
}

Repository::~Repository()
{
    // Jobs only hold copies; wait so none posts back into a dead object.
    m_pool.clear();
    m_pool.waitForDone();
    m_reviewPool.clear();
    m_reviewPool.waitForDone();
    m_searchPool.clear();
    m_searchPool.waitForDone();
}

void Repository::searchCode(const QStringList &texts, bool caseSensitive, bool regex)
{
    if (!valid())
        return;
    // A search queued for an older one is of no use any more
    m_searchPool.clear();
    for (const QString &text : texts) {
        m_searchPool.start([this, git = m_git, text, caseSensitive, regex] {
            QStringList args{QStringLiteral("log"), QStringLiteral("--format=%H"), QStringLiteral("--all")};
            if (!caseSensitive)
                args << QStringLiteral("-i");
            args << (regex ? QStringLiteral("-G") : QStringLiteral("-S")) + text;
            const GitResult result = git.run(args);
            QStringList shas;
            for (const QByteArray &sha : result.out.split('\n')) {
                if (!sha.isEmpty())
                    shas.append(QString::fromLatin1(sha));
            }
            QMetaObject::invokeMethod(this, [this, workDir = git.workDir(), text, caseSensitive, regex, shas] {
                if (workDir == m_git.workDir())
                    m_commits.setCodeMatches(text, caseSensitive, regex, shas);
            }, Qt::QueuedConnection);
        });
    }
}

void Repository::enqueue(JobKind kind, Job job)
{
    ++m_pendingJobs;
    if (kind == JobKind::Write)
        ++m_pendingWrites;
    if (m_pendingJobs == 1 || (kind == JobKind::Write && m_pendingWrites == 1))
        emit busyChanged();

    m_pool.start([this, kind, git = m_git, generation = m_planGeneration, job = std::move(job)] {
        Outcome outcome = job(git);
        if (outcome.workDir.isEmpty())
            outcome.workDir = git.workDir();
        outcome.planGeneration = generation;
        QMetaObject::invokeMethod(this, [this, kind, outcome = std::move(outcome)]() mutable {
            finish(kind, std::move(outcome));
        }, Qt::QueuedConnection);
    });
}

void Repository::finish(JobKind kind, Outcome outcome)
{
    --m_pendingJobs;
    if (kind == JobKind::Write)
        --m_pendingWrites;
    if (m_pendingJobs == 0 || (kind == JobKind::Write && m_pendingWrites == 0))
        emit busyChanged();

    // Stats never change for a commit; whatever mode they were read in.
    if (!outcome.stats.isEmpty() && outcome.workDir == m_git.workDir())
        m_commits.setStats(outcome.stats);

    if (!outcome.openedPath.isEmpty()) {
        m_git.setWorkDir(outcome.openedPath);
        m_gitDir = outcome.gitDir;
        m_historyHead.clear();
        m_statsRequested.clear();
        m_lastFold = {};
        m_diffSource = NoDiff;
        m_diffPath.clear();
        m_diffCommit.clear();
        m_diffPatchSet = 0;
        m_diffBase.clear();
        m_commitDetails.clear();
        ++*m_diffGeneration;
        m_suggester = FoldSuggester();
        m_diff.clear();
        m_usesLfs = false;
        emit lfsChanged();
        emit pathChanged();
        emit diffChanged();
        checkSetup();
        emit commitReviewChanged();
        emit canUndoFoldChanged();
    }

    // Results for a repository that has been replaced meanwhile, or read
    // in or out of plan mode before it was switched, are of no use.
    if (outcome.workDir == m_git.workDir() && outcome.planGeneration == m_planGeneration) {
        if (outcome.trackedFiles)
            m_files.setFiles(*outcome.trackedFiles, outcome.fileTree);
        if (outcome.fold) {
            m_lastFold = *outcome.fold;
            emit canUndoFoldChanged();
        }
        applyState(outcome.state);
        if (outcome.plan)
            m_planModel.setEntries(outcome.plan->entries, outcome.plan->executable, outcome.plan->summary);
        if (outcome.planEnded)
            endPlan();
        if (outcome.committed)
            emit committed();
        if (outcome.patchCommitted)
            emit patchCommitted(outcome.patchCommitted->isEmpty(), *outcome.patchCommitted);
        if (outcome.changeMoved)
            emit changeMoved(outcome.changeMoved->isEmpty(), *outcome.changeMoved);
        if (outcome.message)
            emit messageLoaded(outcome.message->first, outcome.message->second);
        if (outcome.lostWork)
            emit lostWorkFound(*outcome.lostWork);
        if (outcome.sparseFolders)
            emit sparseFoldersLoaded(*outcome.sparseFolders);
        if (outcome.layout) {
            m_shallow = outcome.layout->first;
            m_sparse = outcome.layout->second;
        }
        if (outcome.cleanPreview)
            emit cleanPreviewed(outcome.cleanPreview->first, outcome.cleanPreview->second);
        if (outcome.lfs) {
            m_usesLfs = outcome.lfs->first;
            m_lfsInstalled = outcome.lfs->second;
            emit lfsChanged();
            if (outcome.commitTemplate && *outcome.commitTemplate != m_commitTemplate) {
                m_commitTemplate = *outcome.commitTemplate;
                emit commitTemplateChanged();
            }
            if (m_usesLfs && !m_lfsInstalled)
                emit errorOccurred(tr("This repository keeps files in Git LFS, but git-lfs is not installed: "
                                      "those files show as pointers, and new ones would go into git itself."));
        }
        if (!outcome.pushRejected.isEmpty())
            emit pushRejected(outcome.pushRejected, m_pushTarget.description());
        if (!outcome.notice.isEmpty())
            emit notice(outcome.notice, outcome.links);
    }
    if (!outcome.error.isEmpty())
        emit errorOccurred(outcome.error);
}

void Repository::applyState(const RepositoryState &state)
{
    if (state.status) {
        m_status = *state.status;
        // Undo is only safe while HEAD is still where the fold left it.
        if (canUndoFold() && m_status.headSha != m_lastFold.newHead)
            forgetFold();
        m_unstaged.setEntries(m_status.entries);
        m_staged.setEntries(m_status.entries);
        m_files.setStatus(m_status.entries);
    }
    if (state.history) {
        const HistoryState &history = *state.history;
        m_historyHead = history.head;
        m_historyWip = history.wip;
        m_pushTarget = history.pushTarget;
        scheduleReviewQuery();
        if (!history.reused) {
            m_wipGraph = history.wipGraph;
            m_shownCommits = history.commits;
            m_shownGraph = history.graph;
            m_shownShas.clear();
            for (const CommitInfo &commit : history.commits)
                m_shownShas.insert(commit.sha);
        }
        // What they are for: the filter, and where the refs point
        m_shownFilter = m_plan ? std::nullopt : std::optional<HistoryFilter>(m_filter);
        m_shownTargets = refTargets(history.refs, history.head);
        m_headMessage = history.headMessage;
        m_foldTargets = QSet<QString>(history.foldTargets.cbegin(), history.foldTargets.cend());
        m_editChain = history.foldTargets;
        m_unpublished = history.unpublished;
        m_commits.setCommits(m_shownCommits, m_shownGraph, history.refs, history.head, m_status.branch,
                             history.planned);
        m_branches.clear();
        m_remoteBranches.clear();
        m_tags = history.tagNames;
        m_stashes = history.stashes;
        m_remoteList.clear();
        for (const RemoteInfo &remote : history.remotes)
            m_remoteList.append(QVariantMap{{QStringLiteral("name"), remote.name}, {QStringLiteral("url"), remote.url}});
        m_worktreeList.clear();
        for (const WorktreeInfo &worktree : history.worktrees) {
            if (!worktree.bare)
                m_worktreeList.append(QVariantMap{{QStringLiteral("path"), worktree.path},
                                                  {QStringLiteral("branch"), worktree.branch},
                                                  {QStringLiteral("current"), worktree.current}});
        }
        for (const RefInfo &ref : history.refs) {
            if (ref.kind == RefInfo::LocalBranch)
                m_branches.append(ref.name);
            else if (ref.kind == RefInfo::RemoteBranch && !ref.name.endsWith(QLatin1String("/HEAD")))
                m_remoteBranches.append(ref.name);
        }
        m_branches.sort(Qt::CaseInsensitive);
        m_remoteBranches.sort(Qt::CaseInsensitive);
        if (!history.error.isEmpty())
            emit errorOccurred(history.error);
    }
    if (state.status || state.history)
        emit headChanged();

    if (state.diff && state.diff->request.generation == *m_diffGeneration) {
        const DiffState &diff = *state.diff;
        // Before the model: resetting it clears the selection, which
        // updates the suggestion.
        m_suggester = diff.suggester;
        m_commitDetails = diff.details;
        m_diffConflicted = diff.conflicted;
        if (!diff.message.isEmpty())
            m_diff.setMessage(diff.message);
        else if (diff.request.kind == DiffKind::Content)
            m_diff.setContent(diff.request.path, diff.content, diff.truncated, diff.contentMarkup);
        else
            m_diff.setFiles(diff.files, diff.markup);
        if (!diff.error.isEmpty())
            emit errorOccurred(diff.error);
        emit diffChanged();
    }
    if (!state.error.isEmpty())
        emit errorOccurred(state.error);
}

StateSource Repository::sourceFor(const GitRunner &git, const std::shared_ptr<PlanSession> &plan)
{
    if (plan)
        return plan->source();
    StateSource source;
    source.git = git;
    return source;
}

void Repository::open(const QString &pathOrUrl)
{
    if (m_plan)
        endPlan();
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    enqueue(JobKind::Read, [path, history = freshHistoryRequest()](const GitRunner &) {
        Outcome outcome;
        const GitResult result = GitRunner(path).run({QStringLiteral("rev-parse"),
                                                      QStringLiteral("--show-toplevel")});
        if (!result.ok()) {
            outcome.error = tr("%1 is not inside a git work tree: %2").arg(path, result.errorText());
            return outcome;
        }
        outcome.openedPath = QString::fromUtf8(result.out).trimmed();
        outcome.workDir = outcome.openedPath;
        outcome.gitDir = QString::fromUtf8(GitRunner(outcome.openedPath).run(
            {QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")}).out).trimmed();
        outcome.state = loadState(sourceFor(GitRunner(outcome.openedPath), nullptr), {}, history);
        return outcome;
    });
}

bool Repository::folderExists(const QString &path) const
{
    return !path.isEmpty() && QFileInfo(path).isDir();
}

QString Repository::canonicalPath(const QString &path) const
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? path : canonical;
}

QString Repository::displayPath(const QString &path) const
{
    const QString home = QDir::homePath();
    if (home.isEmpty() || home == QLatin1String("/"))
        return path;
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/')))
        return QLatin1Char('~') + path.mid(home.size());
    return path;
}

void Repository::refresh()
{
    if (!valid())
        return;
    enqueue(JobKind::Read, [request = diffRequest(), plan = m_plan,
                            history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        outcome.state = loadState(sourceFor(git, plan), request, history);
        return outcome;
    });
}

void Repository::refreshStatus()
{
    if (!valid())
        return;
    enqueue(JobKind::Read, [request = diffRequest(), history = historyRequest(false),
                            plan = m_plan](const GitRunner &git) {
        Outcome outcome;
        outcome.state = loadState(sourceFor(git, plan), request, history);
        return outcome;
    });
}

void Repository::showCommit(const QString &sha)
{
    if (sha.isEmpty() || !valid())
        return;
    setDiffView(CommitDiff, {}, sha);
    enqueue(JobKind::Read, [request = diffRequest(), current = m_diffGeneration,
                            plan = m_plan](const GitRunner &git) {
        Outcome outcome;
        // Arrow keys through the history queue a load per row; only the
        // last one matters.
        if (request.generation == *current)
            outcome.state.diff = loadDiff(sourceFor(git, plan), request, {});
        return outcome;
    });
}

void Repository::comparePatchSet(int number)
{
    const ReviewInfo *review = shownReview();
    const GerritPatchSet *patchSet = review ? review->findPatchSet(number) : nullptr;
    if (!patchSet || patchSet->revision.isEmpty())
        return;
    const QString revision = patchSet->revision;
    const QString ref = patchSet->ref;
    setDiffView(PatchSetDiff, {}, m_diffCommit, number);
    m_diffBase = revision;
    enqueue(JobKind::Read, [request = diffRequest(), current = m_diffGeneration, plan = m_plan,
                            remote = m_pushTarget.remote, ref](const GitRunner &git) {
        Outcome outcome;
        if (request.generation != *current)
            return outcome;
        // Patch sets uploaded from elsewhere are not here yet. Fetched
        // without a ref to keep them, as only this comparison needs them.
        if (!git.run({QStringLiteral("cat-file"), QStringLiteral("-e"), request.base + QStringLiteral("^{commit}")})
                 .ok()) {
            const NetworkResult fetched = fetchObjects(git, remote, ref);
            if (!fetched.ok) {
                DiffState diff;
                diff.request = request;
                diff.message = tr("Fetching %1 from %2 failed: %3").arg(ref, remote, fetched.message);
                outcome.state.diff = diff;
                return outcome;
            }
        }
        outcome.state.diff = loadDiff(sourceFor(git, plan), request, {});
        return outcome;
    });
}

void Repository::showFile(const QString &path, bool staged)
{
    if (!valid())
        return;
    setDiffView(staged ? StagedDiff : UnstagedDiff, path, {});
    enqueue(JobKind::Read, [request = diffRequest(), history = historyRequest(false),
                            current = m_diffGeneration, plan = m_plan](const GitRunner &git) {
        Outcome outcome;
        if (request.generation == *current)
            outcome.state = loadState(sourceFor(git, plan), request, history);
        return outcome;
    });
}

void Repository::showFileContent(const QString &path)
{
    if (!valid())
        return;
    setDiffView(FileContent, path, {});
    enqueue(JobKind::Read, [request = diffRequest(), current = m_diffGeneration,
                            plan = m_plan](const GitRunner &git) {
        Outcome outcome;
        if (request.generation == *current)
            outcome.state.diff = loadDiff(sourceFor(git, plan), request, {});
        return outcome;
    });
}

void Repository::showWorkingTree()
{
    if (m_unstaged.count() > 0)
        showFile(m_unstaged.data(m_unstaged.index(0), FileListModel::PathRole).toString(), false);
    else if (m_staged.count() > 0)
        showFile(m_staged.data(m_staged.index(0), FileListModel::PathRole).toString(), true);
}

void Repository::loadFiles()
{
    if (!valid())
        return;
    enqueue(JobKind::Read, [plan = m_plan](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = sourceFor(git, plan).git.run({QStringLiteral("ls-files"), QStringLiteral("-z")});
        QStringList paths;
        for (const QByteArray &path : result.out.split('\0')) {
            if (!path.isEmpty())
                paths.append(QString::fromUtf8(path));
        }
        outcome.fileTree = buildFileTree(paths);
        outcome.trackedFiles = paths;
        return outcome;
    });
}

void Repository::requestStats(const QString &sha)
{
    if (sha.isEmpty() || m_commits.hasStats(sha) || m_statsRequested.contains(sha))
        return;
    m_statsRequested.insert(sha);
    m_statsWanted.append(sha);
    m_statsTimer.start();
}

void Repository::loadRequestedStats()
{
    constexpr int BatchSize = 100;
    const QStringList batch = m_statsWanted.mid(0, BatchSize);
    m_statsWanted.remove(0, batch.size());
    if (!m_statsWanted.isEmpty())
        m_statsTimer.start();
    enqueue(JobKind::Read, [batch](const GitRunner &git) {
        Outcome outcome;
        outcome.stats = loadCommitStats(git, batch);
        return outcome;
    });
}

QVariantMap Repository::wipCounts() const
{
    int modified = 0;
    int added = 0;
    int deleted = 0;
    for (const StatusEntry &e : m_status.entries) {
        if (e.untracked || e.index == QLatin1Char('A'))
            ++added;
        else if (e.index == QLatin1Char('D') || e.worktree == QLatin1Char('D'))
            ++deleted;
        else
            ++modified;
    }
    return {
        {QStringLiteral("modified"), modified},
        {QStringLiteral("added"), added},
        {QStringLiteral("deleted"), deleted},
    };
}

void Repository::setShowRemotes(bool show)
{
    if (m_filter.remotes == show)
        return;
    m_filter.remotes = show;
    emit filterChanged();
    refresh();
}

void Repository::setCurrentBranchOnly(bool only)
{
    if (m_filter.currentBranchOnly == only)
        return;
    m_filter.currentBranchOnly = only;
    emit filterChanged();
    refresh();
}

void Repository::setShowTags(bool show)
{
    if (m_filter.tags == show)
        return;
    m_filter.tags = show;
    emit filterChanged();
    refresh();
}

void Repository::stageFile(const QString &path)
{
    stageFiles({path});
}

void Repository::unstageFile(const QString &path)
{
    unstageFiles({path});
}

void Repository::stageFiles(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const QString what = paths.size() == 1 ? paths.first() : tr("%n file(s)", "", int(paths.size()));
    // Follow the file to the side it moves to.
    if (m_diffSource == UnstagedDiff && paths.contains(m_diffPath))
        setDiffView(StagedDiff, m_diffPath, {});
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::StagePaths;
        entry.paths = paths;
        entry.title = tr("Stage %1").arg(what);
        enqueuePlanEntry(entry);
        return;
    }
    enqueueCommand(tr("Staging %1").arg(what), QStringList{QStringLiteral("add"), QStringLiteral("--")} + paths);
}

void Repository::unstageFiles(const QStringList &files)
{
    if (files.isEmpty())
        return;
    QStringList paths = files;
    for (const QString &path : files) {
        if (const StatusEntry *entry = m_staged.entry(path); entry && !entry->origPath.isEmpty())
            paths.append(entry->origPath);
    }
    const QString what = files.size() == 1 ? files.first() : tr("%n file(s)", "", int(files.size()));
    if (m_diffSource == StagedDiff && files.contains(m_diffPath))
        setDiffView(UnstagedDiff, m_diffPath, {});
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::UnstagePaths;
        entry.paths = paths;
        entry.title = tr("Unstage %1").arg(what);
        enqueuePlanEntry(entry);
        return;
    }

    const QStringList command = m_status.headSha.isEmpty()
        ? QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-q")}
        : QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD")};
    enqueueCommand(tr("Unstaging %1").arg(what), command + QStringList{QStringLiteral("--")} + paths);
}

void Repository::stageAll()
{
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::StagePaths;
        entry.title = tr("Stage all changes");
        enqueuePlanEntry(entry);
        return;
    }
    enqueueCommand(tr("Staging all changes"), {QStringLiteral("add"), QStringLiteral("-A")});
}

void Repository::unstageAll()
{
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::UnstageAll;
        entry.title = tr("Unstage all changes");
        enqueuePlanEntry(entry);
        return;
    }
    const QStringList command = m_status.headSha.isEmpty()
        ? QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"),
                      QStringLiteral("-q"), QStringLiteral(".")}
        : QStringList{QStringLiteral("reset"), QStringLiteral("-q")};
    enqueueCommand(tr("Unstaging all changes"), command);
}

void Repository::applySelection()
{
    if ((m_diffSource != UnstagedDiff && m_diffSource != StagedDiff) || m_ignoreWhitespace)
        return;
    const bool unstage = m_diffSource == StagedDiff;
    const QByteArray patch = selectionPatch(unstage ? PatchDirection::Reverse : PatchDirection::Forward);
    if (patch.isEmpty())
        return;

    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::ApplyPatch;
        entry.patch = patch;
        entry.reverse = unstage;
        const int lines = m_diff.selectedCount();
        entry.title = unstage ? tr("Unstage %n line(s) of %1", "", lines).arg(m_diffPath)
                              : tr("Stage %n line(s) of %1", "", lines).arg(m_diffPath);
        enqueuePlanEntry(entry);
        return;
    }

    QStringList args{QStringLiteral("apply"), QStringLiteral("--cached"),
                     QStringLiteral("--whitespace=nowarn")};
    if (unstage)
        args.append(QStringLiteral("--reverse"));
    enqueueCommand(unstage ? tr("Unstaging lines") : tr("Staging lines"), args, patch);
}

void Repository::applyHunk(int row)
{
    if (!m_diff.inHunk(row))
        return;
    m_diff.selectHunk(row);
    applySelection();
}

void Repository::applyLine(int row)
{
    if (!m_diff.isRowSelected(row)) {
        m_diff.clearSelection();
        m_diff.toggleLine(row);
    }
    applySelection();
}

void Repository::resolveConflict(const QString &path, const QString &content)
{
    enqueue(JobKind::Write, [path, data = content.toUtf8(), request = diffRequest(),
                             history = historyRequest(false)](const GitRunner &git) {
        Outcome outcome;
        QFile file(QDir(git.workDir()).filePath(path));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
            outcome.error = tr("Could not write %1: %2").arg(path, file.errorString());
        } else {
            file.close();
            const GitResult added = git.run({QStringLiteral("add"), QStringLiteral("--"), path});
            if (!added.ok())
                outcome.error = tr("Marking %1 as resolved failed: %2").arg(path, added.errorText());
            else
                outcome.notice = tr("Resolved %1.").arg(path);
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::resolveWithSide(const QString &path, bool theirs)
{
    enqueue(JobKind::Write, [path, theirs, request = diffRequest(),
                             history = historyRequest(false)](const GitRunner &git) {
        Outcome outcome;
        // Stage 2 is our version, 3 theirs; a missing one means deleted.
        const QByteArray stages = git.run({QStringLiteral("ls-files"), QStringLiteral("-u"),
                                           QStringLiteral("--"), path}).out;
        const bool exists = stages.contains(theirs ? " 3\t" : " 2\t");
        const GitResult result = exists
            ? git.run({QStringLiteral("checkout"), theirs ? QStringLiteral("--theirs") : QStringLiteral("--ours"),
                       QStringLiteral("--"), path})
            : git.run({QStringLiteral("rm"), QStringLiteral("-q"), QStringLiteral("--"), path});
        GitResult added = result;
        if (result.ok() && exists)
            added = git.run({QStringLiteral("add"), QStringLiteral("--"), path});
        if (!added.ok())
            outcome.error = tr("Resolving %1 failed: %2").arg(path, added.errorText());
        else
            outcome.notice = tr("Resolved %1.").arg(path);
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::commit(const QString &message, bool amend, bool skipHooks, bool resetAuthor)
{
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::Commit;
        entry.message = message;
        entry.amend = amend;
        entry.skipHooks = skipHooks;
        entry.resetAuthor = amend && resetAuthor;
        const QString subject = message.section(QLatin1Char('\n'), 0, 0).trimmed();
        entry.title = amend ? tr("Amend the last commit as “%1”").arg(subject)
                            : tr("Commit “%1”").arg(subject);
        enqueuePlanEntry(entry);
        return;
    }

    QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend)
        args.append(QStringLiteral("--amend"));
    if (amend && resetAuthor)
        args.append(QStringLiteral("--reset-author"));
    if (skipHooks)
        args.append(QStringLiteral("--no-verify"));
    enqueue(JobKind::Write, [args, input = message.toUtf8(), request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run(args, input);
        outcome.committed = result.ok();
        if (!result.ok())
            outcome.error = tr("Committing failed: %1").arg(result.errorText());
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::commitPatch(const QByteArray &patch, const QString &message)
{
    const QString subject = message.section(QLatin1Char('\n'), 0, 0).trimmed();
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::CommitPatch;
        entry.patch = patch;
        entry.message = message;
        entry.title = tr("Commit “%1”").arg(subject);
        enqueuePlanEntry(entry);
        return;
    }
    enqueue(JobKind::Write, [patch, message, subject, request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        QString error;
        if (::commitPatch(git, patch, message, error).isEmpty()) {
            outcome.error = tr("Committing “%1” failed: %2").arg(subject, error);
            outcome.patchCommitted = error;
        } else {
            // Committed; error may say what went wrong after it
            outcome.error = error;
            outcome.patchCommitted = QString();
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::moveChange(const QString &from, const QString &to, const QByteArray &patch, const QString &title)
{
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::MoveChange;
        entry.source = from;
        entry.target = to;
        entry.patch = patch;
        entry.title = title;
        enqueuePlanEntry(entry);
        return;
    }
    enqueue(JobKind::Write, [=, request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const FoldResult result = ::moveChange(git, from, to, patch);
        if (!result.ok) {
            QString error = result.error;
            if (!result.conflicts.isEmpty())
                error += QLatin1Char(' ') + tr("Conflicting files: %1").arg(result.conflicts.join(QStringLiteral(", ")));
            outcome.error = tr("%1 failed: %2").arg(title, error);
            outcome.changeMoved = error;
        } else {
            // The index of the change's paths follows HEAD, so the change
            // shows where it is now and nowhere else; undoing carries it
            // back along with HEAD
            const GitResult reset = git.run(QStringList{QStringLiteral("reset"), QStringLiteral("-q"),
                                                        QStringLiteral("HEAD"), QStringLiteral("--")}
                                            + patchPaths(patch));
            if (!reset.ok())
                outcome.error = tr("Moved, but the index of its files could not follow: %1").arg(reset.errorText());
            outcome.fold = LastFold{result.oldHead, result.newHead, {}, true, true};
            outcome.changeMoved = QString();
            outcome.notice = tr("%1: rewrote %n commit(s).", "", result.rewritten).arg(title);
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

QString Repository::foldBlocker(const QString &sha) const
{
    if (m_diffSource != UnstagedDiff && m_diffSource != StagedDiff)
        return tr("Show a working tree file and select the lines to fold.");
    if (m_diff.selectedCount() == 0 && !m_ignoreWhitespace)
        return tr("Select the lines to fold first.");
    return foldTargetBlocker(sha);
}

QString Repository::foldTargetBlocker(const QString &sha) const
{
    if (m_ignoreWhitespace)
        return tr("Lines cannot be folded while white space is ignored.");
    if (!m_foldTargets.contains(sha))
        return tr("Only commits of the current branch, from HEAD down to its last merge, "
                  "can be folded into.");
    return {};
}

bool Repository::isPublished(const QString &sha) const
{
    return m_foldTargets.contains(sha) && !m_unpublished.contains(sha);
}

void Repository::foldSelection(const QString &sha)
{
    if (const QString reason = foldBlocker(sha); !reason.isEmpty()) {
        emit errorOccurred(reason);
        return;
    }

    const int lines = m_diff.selectedCount();
    const QString target = commitLabel(sha);
    const QByteArray patch = selectionPatch(PatchDirection::Forward);
    const FoldBase base = m_diffSource == StagedDiff ? FoldBase::Head : FoldBase::Index;

    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::Fold;
        entry.target = sha;
        entry.patch = patch;
        entry.base = base;
        entry.title = tr("Fold %n line(s) into %1", "", lines).arg(target);
        enqueuePlanEntry(entry);
        return;
    }

    enqueue(JobKind::Write, [=, request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const FoldResult result = FoldEngine(git).fold(sha, patch, base);
        if (!result.ok) {
            outcome.error = tr("Folding failed: %1").arg(result.error);
            if (!result.conflicts.isEmpty())
                outcome.error += QLatin1Char(' ')
                    + tr("Conflicting files: %1").arg(result.conflicts.join(QStringLiteral(", ")));
        } else {
            outcome.fold = LastFold{result.oldHead, result.newHead,
                                    result.indexUpdated ? patch : QByteArray()};
            if (!result.error.isEmpty())
                outcome.error = result.error;
            else
                outcome.notice = tr("Folded %n line(s) into %1 and rewrote %2 commit(s).", "", lines)
                                     .arg(target).arg(result.rewritten);
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::undoFold()
{
    if (!canUndoFold() || m_plan)
        return;
    const LastFold fold = m_lastFold;
    forgetFold();

    enqueue(JobKind::Write, [fold, request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const GitResult moved = git.run({QStringLiteral("update-ref"), QStringLiteral("-m"),
                                         fold.edit ? QStringLiteral("igitt: undo history edit")
                                                   : QStringLiteral("igitt: undo fold"),
                                         QStringLiteral("HEAD"), fold.oldHead, fold.newHead});
        if (!moved.ok()) {
            outcome.error = fold.edit ? tr("Undoing the history edit failed: %1").arg(moved.errorText())
                                      : tr("Undoing the fold failed: %1").arg(moved.errorText());
        } else if (fold.indexCarried) {
            const GitResult carried = git.run({QStringLiteral("read-tree"), QStringLiteral("-m"),
                                               QStringLiteral("-i"), fold.newHead, fold.oldHead});
            if (!carried.ok())
                outcome.error = tr("The history edit was undone, but the index could not follow: %1")
                                    .arg(carried.errorText());
        } else if (!fold.stagedPatch.isEmpty()) {
            const GitResult unstaged = git.run({QStringLiteral("apply"), QStringLiteral("--cached"),
                                                QStringLiteral("--reverse"), QStringLiteral("--whitespace=nowarn")},
                                               fold.stagedPatch);
            if (!unstaged.ok())
                outcome.error = tr("The fold was undone, but unstaging the folded lines failed: %1")
                                    .arg(unstaged.errorText());
        }
        if (outcome.error.isEmpty())
            outcome.notice = fold.edit ? tr("History edit undone.") : tr("Fold undone.");
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

QString Repository::editBlocker(const QString &kind, const QString &sha, const QString &to) const
{
    if (const QString op = operation(); !op.isEmpty())
        return tr("git is in the middle of an operation (%1); finish or abort it first.").arg(op);
    const qsizetype i = m_editChain.indexOf(sha);
    if (i < 0)
        return tr("Only commits of the current branch, from HEAD down to its last merge, can be edited.");
    if (kind == QLatin1StringView("reword"))
        return {};
    if (isMergeCommit(sha))
        return tr("A merge commit can only be reworded.");
    if (kind == QLatin1StringView("moveUp"))
        return i == 0 ? tr("It is the latest commit already.") : QString();
    if (kind == QLatin1StringView("drop"))
        return m_editChain.size() == 1 ? tr("Dropping the only commit would leave the branch empty.") : QString();
    if (kind == QLatin1StringView("moveDown") || kind == QLatin1StringView("squash")
        || kind == QLatin1StringView("fixup")) {
        if (i + 1 >= m_editChain.size())
            return tr("There is no commit below it that can be edited.");
        if (isMergeCommit(m_editChain[i + 1]))
            return tr("The commit below it is a merge commit.");
        return {};
    }
    if (kind == QLatin1StringView("move")) {
        const qsizetype j = m_editChain.indexOf(to);
        if (j < 0)
            return tr("Commits can only be moved within the current branch, from HEAD down to its last merge.");
        if (j == i)
            return tr("Drop it onto another commit to move it there.");
        if (isMergeCommit(to))
            return tr("Commits cannot be moved past a merge commit.");
        return {};
    }
    return tr("Unknown edit “%1”.").arg(kind);
}

bool Repository::editRewritesPublished(const QString &kind, const QString &sha, const QString &to) const
{
    const QString oldest = oldestEdited(kind, sha, to);
    return !oldest.isEmpty() && isPublished(oldest);
}

QString Repository::oldestEdited(const QString &kind, const QString &sha, const QString &to) const
{
    if (!editBlocker(kind, sha, to).isEmpty())
        return {};
    const qsizetype i = m_editChain.indexOf(sha);
    if (kind == QLatin1StringView("moveDown") || kind == QLatin1StringView("squash")
        || kind == QLatin1StringView("fixup"))
        return m_editChain[i + 1];
    if (kind == QLatin1StringView("move"))
        return m_editChain[std::max(i, m_editChain.indexOf(to))]; // HEAD first: older is further down
    return sha;
}

void Repository::editCommit(const QString &kind, const QString &sha, const QString &to, const QString &message)
{
    if (const QString reason = editBlocker(kind, sha, to); !reason.isEmpty()) {
        emit errorOccurred(reason);
        return;
    }
    const qsizetype i = m_editChain.indexOf(sha);
    const QString label = commitLabel(sha);
    HistoryEdit edit;
    edit.commit = sha;
    QString title;
    if (kind == QLatin1StringView("reword")) {
        edit.kind = HistoryEdit::Reword;
        edit.message = message;
        title = tr("Reword %1").arg(label);
    } else if (kind == QLatin1StringView("moveUp")) {
        edit.kind = HistoryEdit::Move;
        edit.to = m_editChain[i - 1];
        title = tr("Move %1 up").arg(label);
    } else if (kind == QLatin1StringView("moveDown")) {
        edit.kind = HistoryEdit::Move;
        edit.to = m_editChain[i + 1];
        title = tr("Move %1 down").arg(label);
    } else if (kind == QLatin1StringView("move")) {
        edit.kind = HistoryEdit::Move;
        edit.to = to;
        title = tr("Move %1 to where %2 is").arg(label, commitLabel(to));
    } else if (kind == QLatin1StringView("squash")) {
        edit.kind = HistoryEdit::Squash;
        title = tr("Squash %1 into %2").arg(label, commitLabel(m_editChain[i + 1]));
    } else if (kind == QLatin1StringView("fixup")) {
        edit.kind = HistoryEdit::Fixup;
        title = tr("Fix up %1 with %2").arg(commitLabel(m_editChain[i + 1]), label);
    } else {
        edit.kind = HistoryEdit::Drop;
        title = tr("Drop %1").arg(label);
    }

    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::EditHistory;
        entry.edit = edit;
        entry.title = title;
        enqueuePlanEntry(entry);
        return;
    }

    enqueue(JobKind::Write, [edit, title, request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const RewriteResult result = HistoryEditor(git).edit(edit);
        if (!result.ok) {
            outcome.error = tr("%1 failed: %2").arg(title, result.error);
            if (!result.conflicts.isEmpty())
                outcome.error += QLatin1Char(' ')
                    + tr("Conflicting files: %1").arg(result.conflicts.join(QStringLiteral(", ")));
        } else if (result.newHead == result.oldHead) {
            outcome.notice = tr("Nothing changed.");
        } else {
            outcome.fold = LastFold{result.oldHead, result.newHead, {}, true, result.indexUpdated};
            if (!result.error.isEmpty())
                outcome.error = result.error;
            else
                outcome.notice = tr("%1: rewrote %n commit(s).", "", result.rewritten).arg(title);
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::requestMessage(const QString &sha)
{
    if (sha.isEmpty() || !valid())
        return;
    enqueue(JobKind::Read, [sha, plan = m_plan](const GitRunner &git) {
        Outcome outcome;
        const GitResult r = sourceFor(git, plan).git.run({QStringLiteral("log"), QStringLiteral("-1"),
                                                          QStringLiteral("--format=%B"), sha});
        if (r.ok())
            outcome.message = std::make_pair(sha, QString::fromUtf8(r.out));
        else
            outcome.error = tr("Reading the message of %1 failed: %2").arg(sha.left(10), r.errorText());
        return outcome;
    });
}

void Repository::startPlanning()
{
    if (m_plan || !valid())
        return;
    forgetFold();
    m_plan = std::make_shared<PlanSession>();
    ++m_planGeneration;
    m_planModel.clear();
    emit planningChanged();

    enqueue(JobKind::Read, [plan = m_plan, request = diffRequest(),
                            history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        QString error;
        if (!plan->begin(git, error)) {
            outcome.error = tr("Could not start planning: %1").arg(error);
            outcome.planEnded = true;
            return outcome;
        }
        outcome.plan = snapshot(*plan);
        outcome.state = loadState(plan->source(), request, history);
        return outcome;
    });
}

void Repository::discardPlan()
{
    if (!m_plan)
        return;
    endPlan();
    refresh();
}

void Repository::executePlan()
{
    if (!m_plan)
        return;
    enqueue(JobKind::Write, [plan = m_plan, request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        QString error;
        const int count = int(plan->entries().size());
        if (!plan->execute(error)) {
            outcome.error = tr("The plan was not executed: %1").arg(error);
            outcome.plan = snapshot(*plan);
            outcome.state = loadState(plan->source(), request, history);
            return outcome;
        }
        outcome.planEnded = true;
        outcome.notice = tr("Executed %n planned action(s).", "", count);
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::removePlanEntry(int entryId)
{
    if (!m_plan)
        return;
    enqueue(JobKind::Write, [plan = m_plan, entryId, request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &) {
        Outcome outcome;
        plan->remove(entryId);
        outcome.plan = snapshot(*plan);
        outcome.state = loadState(plan->source(), request, history);
        return outcome;
    });
}

void Repository::enqueuePlanEntry(const PlanEntry &entry)
{
    enqueue(JobKind::Write, [plan = m_plan, entry, request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &) {
        Outcome outcome;
        QString error;
        const bool added = plan->add(entry, error);
        if (added)
            outcome.committed = entry.kind == PlanEntry::Commit;
        else
            outcome.error = tr("Could not plan “%1”: %2").arg(entry.title, error);
        if (entry.kind == PlanEntry::CommitPatch)
            outcome.patchCommitted = added ? QString() : error;
        if (entry.kind == PlanEntry::MoveChange)
            outcome.changeMoved = added ? QString() : error;
        outcome.plan = snapshot(*plan);
        outcome.state = loadState(plan->source(), request, history);
        return outcome;
    });
}

void Repository::endPlan()
{
    m_plan.reset();
    ++m_planGeneration;
    m_planModel.clear();
    emit planningChanged();
}

Repository::PlanSnapshot Repository::snapshot(const PlanSession &plan)
{
    return {plan.entries(), plan.executable(), plan.publishSummary()};
}

void Repository::enqueueCommand(const QString &what, const QStringList &args, const QByteArray &input)
{
    enqueue(JobKind::Write, [=, request = diffRequest(),
                             history = historyRequest(false)](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run(args, input);
        if (!result.ok())
            outcome.error = tr("%1 failed: %2").arg(what, result.errorText());
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::setDiffView(DiffSource source, const QString &path, const QString &commit, int patchSet)
{
    m_diffSource = source;
    m_diffPath = path;
    m_diffCommit = commit;
    m_diffPatchSet = patchSet;
    m_diffBase.clear();
    if (source != CommitDiff && source != PatchSetDiff && source != CompareDiff)
        m_commitDetails.clear();
    m_diffConflicted = false;
    ++*m_diffGeneration;
    emit diffChanged();
    emit commitReviewChanged();
}

DiffRequest Repository::diffRequest() const
{
    DiffRequest request;
    request.kind = diffKind(m_diffSource);
    request.path = m_diffPath;
    request.commit = m_diffCommit;
    request.generation = *m_diffGeneration;
    request.ignoreWhitespace = m_ignoreWhitespace;
    if (m_diffSource == PatchSetDiff) {
        request.base = m_diffBase;
        request.baseName = tr("patch set %1").arg(m_diffPatchSet);
        request.patchSet = true;
    } else if (m_diffSource == CompareDiff) {
        request.base = m_diffBase;
        request.baseName = m_diffBaseName;
        request.threeDot = m_diffThreeDot;
    }
    return request;
}

void Repository::reloadDiff()
{
    const QString path = m_diffPath;
    const QString commit = m_diffCommit;
    switch (m_diffSource) {
    case UnstagedDiff:
    case StagedDiff:
        showFile(path, m_diffSource == StagedDiff);
        break;
    case CommitDiff:
        showCommit(commit);
        break;
    case PatchSetDiff:
        comparePatchSet(m_diffPatchSet);
        break;
    case CompareDiff:
        compareCommits(m_diffBase, commit, m_diffBaseName, m_diffThreeDot);
        break;
    case FileContent:
    case NoDiff:
        break;
    }
}

void Repository::setIgnoreWhitespace(bool ignore)
{
    if (m_ignoreWhitespace == ignore)
        return;
    m_ignoreWhitespace = ignore;
    // A selection made in one diff means nothing in the other.
    m_diff.clearSelection();
    emit ignoreWhitespaceChanged();
    reloadDiff();
}

HistoryRequest Repository::freshHistoryRequest() const
{
    HistoryRequest request{true, {}, false, m_filter, m_pushSettings};
    return request;
}

HistoryRequest Repository::historyRequest(bool force) const
{
    HistoryRequest request{force, m_historyHead, m_historyWip, m_filter, m_pushSettings};
    // Not while planning: the simulation reads it all
    if (!m_plan) {
        request.knownFilter = m_shownFilter;
        request.knownTargets = m_shownTargets;
        request.knownCommits = m_shownShas;
    }
    return request;
}

void Repository::scheduleReviewQuery()
{
    if (m_pushTarget.host == ReviewHost::None || !m_pushTarget.error.isEmpty() || m_plan
        || m_status.headSha.isEmpty()) {
        m_reviewTimer.stop();
        ++m_reviewGeneration;
        setReviews({});
        return;
    }
    m_reviewTimer.start();
}

void Repository::queryReviewsNow()
{
    const ReviewProvider *provider = reviewProvider(m_pushTarget.host);
    if (!provider)
        return;
    const int generation = ++m_reviewGeneration;
    ReviewContext context;
    context.remote = m_pushTarget.remote;
    context.upstream = !m_status.upstream.isEmpty()
        ? m_status.upstream
        : QStringLiteral("refs/remotes/%1/%2").arg(m_pushTarget.remote, m_pushTarget.remoteBranch);
    context.branch = m_pushTarget.remoteBranch;
    context.head = m_status.headSha;
    m_reviewPool.start([this, generation, git = m_git, provider, context] {
        ReviewQuery reviews = provider->query(git, context);
        reviews.head = context.head;
        QMetaObject::invokeMethod(this, [this, generation, reviews = std::move(reviews)] {
            if (generation == m_reviewGeneration)
                setReviews(reviews);
        }, Qt::QueuedConnection);
    });
}

namespace {

QVariantMap reviewMap(const BranchReview &review, const QString &head)
{
    static const char *const states[] = {"open", "merged", "closed"};
    static const char *const decisions[] = {"", "approved", "changesRequested", "reviewRequired"};
    static const char *const checks[] = {"", "pending", "success", "failure"};
    return {
        {QStringLiteral("kind"), review.host == ReviewHost::GitLab ? QStringLiteral("mergeRequest")
                                                                   : QStringLiteral("pullRequest")},
        {QStringLiteral("state"), QString::fromLatin1(states[int(review.state)])},
        {QStringLiteral("number"), review.number},
        {QStringLiteral("label"), review.label()},
        {QStringLiteral("title"), review.title},
        {QStringLiteral("url"), review.url},
        {QStringLiteral("draft"), review.draft},
        {QStringLiteral("decision"), QString::fromLatin1(decisions[int(review.decision)])},
        {QStringLiteral("checks"), QString::fromLatin1(checks[int(review.checks)])},
        {QStringLiteral("unresolved"), review.unresolved ? QVariant(*review.unresolved) : QVariant()},
        {QStringLiteral("badges"), review.badges()},
        {QStringLiteral("details"), review.details(head)},
    };
}

} // namespace

void Repository::setReviews(const ReviewQuery &reviews)
{
    m_reviews = reviews;
    QHash<QString, QVariantMap> bySha;
    for (auto it = reviews.bySha.cbegin(); it != reviews.bySha.cend(); ++it) {
        static const char *const states[] = {"new", "upToDate", "outdated", "closed"};
        bySha.insert(it.key(), {
            {QStringLiteral("kind"), QStringLiteral("change")},
            {QStringLiteral("state"), QString::fromLatin1(states[it->state])},
            {QStringLiteral("number"), it->number},
            {QStringLiteral("patchSet"), it->patchSet},
            {QStringLiteral("url"), it->url},
            {QStringLiteral("wip"), it->wip},
            {QStringLiteral("topic"), it->topic},
            {QStringLiteral("hashtags"), it->hashtags},
            {QStringLiteral("codeReview"), it->shownVote(QStringLiteral("Code-Review"))},
            {QStringLiteral("verified"), it->shownVote(QStringLiteral("Verified"))},
            {QStringLiteral("comments"), int(it->comments.size())},
            {QStringLiteral("unresolved"), it->unresolved ? QVariant(*it->unresolved) : QVariant()},
            {QStringLiteral("badges"), it->badges()},
            {QStringLiteral("details"), it->details()},
        });
    }
    // A pull or merge request belongs to the branch; it shows on its tip.
    if (reviews.branchReview && !reviews.head.isEmpty())
        bySha.insert(reviews.head, reviewMap(*reviews.branchReview, reviews.head));
    m_commits.setReviews(bySha);
    emit reviewsChanged();
    emit commitReviewChanged();
}

const ReviewInfo *Repository::shownReview() const
{
    if (m_diffSource != CommitDiff && m_diffSource != PatchSetDiff)
        return nullptr;
    const auto it = m_reviews.bySha.constFind(m_diffCommit);
    return it != m_reviews.bySha.cend() && it->number > 0 ? &*it : nullptr;
}

QVariantMap Repository::commitReview() const
{
    const ReviewInfo *review = shownReview();
    if (!review)
        return {};
    // The patch sets the diff on display shows, whose comments it marks.
    const int oldSide = diffPatchSet();
    const int newSide = review->localPatchSet;

    QVariantList messages;
    for (const GerritMessage &message : review->messages) {
        messages.append(QVariantMap{{QStringLiteral("author"), message.author},
                                    {QStringLiteral("date"), message.date},
                                    {QStringLiteral("text"), message.text}});
    }
    QVariantList patchSets;
    QVariantList comments;
    for (auto set = review->patchSets.crbegin(); set != review->patchSets.crend(); ++set) {
        patchSets.append(QVariantMap{
            {QStringLiteral("number"), set->number},
            {QStringLiteral("revision"), set->revision},
            {QStringLiteral("uploader"), set->uploader},
            {QStringLiteral("date"), set->created},
            {QStringLiteral("kind"), set->kind},
            {QStringLiteral("kindText"), patchSetKindText(set->kind)},
            {QStringLiteral("comments"), int(set->comments.size())},
            {QStringLiteral("current"), set->number == review->patchSet},
            {QStringLiteral("local"), set->number == review->localPatchSet},
        });
        for (const GerritComment &comment : set->comments) {
            const bool marked = comment.line > 0 && (set->number == oldSide || set->number == newSide);
            comments.append(QVariantMap{{QStringLiteral("patchSet"), comment.patchSet},
                                        {QStringLiteral("file"), comment.file},
                                        {QStringLiteral("line"), comment.line},
                                        {QStringLiteral("author"), comment.author},
                                        {QStringLiteral("text"), comment.text},
                                        {QStringLiteral("marked"), marked}});
        }
    }
    return {
        {QStringLiteral("number"), review->number},
        {QStringLiteral("url"), review->url},
        {QStringLiteral("patchSet"), review->patchSet},
        {QStringLiteral("localPatchSet"), review->localPatchSet},
        {QStringLiteral("unresolved"), review->unresolved ? QVariant(*review->unresolved) : QVariant()},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("comments"), comments},
        {QStringLiteral("patchSets"), patchSets},
    };
}

QVariantMap Repository::diffComments() const
{
    const ReviewInfo *review = shownReview();
    if (!review)
        return {};
    const auto sideOf = [review](int number) {
        QVariantMap byLine;
        const GerritPatchSet *set = number > 0 ? review->findPatchSet(number) : nullptr;
        if (!set)
            return byLine;
        for (const GerritComment &comment : set->comments) {
            if (comment.line <= 0)
                continue;
            const QString key = comment.file + QLatin1Char(':') + QString::number(comment.line);
            QVariantList list = byLine.value(key).toList();
            list.append(QVariantMap{{QStringLiteral("author"), comment.author},
                                    {QStringLiteral("text"), comment.text}});
            byLine.insert(key, list);
        }
        return byLine;
    };
    return {{QStringLiteral("oldSide"), sideOf(diffPatchSet())},
            {QStringLiteral("newSide"), sideOf(review->localPatchSet)}};
}

QString Repository::reviewHost() const
{
    switch (m_pushTarget.host) {
    case ReviewHost::Gerrit: return QStringLiteral("gerrit");
    case ReviewHost::GitHub: return QStringLiteral("github");
    case ReviewHost::GitLab: return QStringLiteral("gitlab");
    case ReviewHost::None: break;
    }
    return {};
}

QVariantMap Repository::branchReview() const
{
    return m_reviews.branchReview ? reviewMap(*m_reviews.branchReview, m_reviews.head) : QVariantMap();
}

bool Repository::pushNeeded() const
{
    // Without an answer from Gerrit, leave it to the user.
    if (!m_pushTarget.forReview || !m_reviews.ok)
        return true;
    return std::any_of(m_reviews.bySha.cbegin(), m_reviews.bySha.cend(), [](const ReviewInfo &info) {
        return info.state == ReviewInfo::New || info.state == ReviewInfo::Outdated;
    });
}

QString Repository::reviewSummary() const
{
    if (m_pushTarget.host == ReviewHost::None || m_reviews.host != m_pushTarget.host)
        return {};
    if (!m_reviews.ok)
        return m_reviews.error;
    if (m_reviews.host != ReviewHost::Gerrit) {
        if (m_reviews.branchReview)
            return m_reviews.branchReview->summary();
        return m_reviews.host == ReviewHost::GitLab ? tr("No merge request for %1 yet.").arg(m_reviews.branch)
                                                    : tr("No pull request for %1 yet.").arg(m_reviews.branch);
    }
    int counts[4] = {};
    for (const ReviewInfo &info : m_reviews.bySha)
        ++counts[info.state];
    if (m_reviews.bySha.isEmpty())
        return tr("Nothing to push for review.");
    QStringList parts;
    if (counts[ReviewInfo::UpToDate])
        parts << tr("%n up to date on Gerrit", "", counts[ReviewInfo::UpToDate]);
    if (counts[ReviewInfo::Outdated])
        parts << tr("%n changed since the last upload", "", counts[ReviewInfo::Outdated]);
    if (counts[ReviewInfo::New])
        parts << tr("%n new", "", counts[ReviewInfo::New]);
    if (counts[ReviewInfo::Closed])
        parts << tr("%n merged or abandoned", "", counts[ReviewInfo::Closed]);
    return tr("%n commit(s) to review: %1.", "", int(m_reviews.bySha.size())).arg(parts.join(QStringLiteral(", ")));
}

QString Repository::pushMode() const
{
    switch (m_pushSettings.mode) {
    case PushMode::Plain: return QStringLiteral("plain");
    case PushMode::Gerrit: return QStringLiteral("gerrit");
    case PushMode::GitHub: return QStringLiteral("github");
    case PushMode::GitLab: return QStringLiteral("gitlab");
    case PushMode::Auto: break;
    }
    return QStringLiteral("auto");
}

void Repository::setPushMode(const QString &mode)
{
    const PushMode value = mode == QLatin1StringView("plain")    ? PushMode::Plain
                         : mode == QLatin1StringView("gerrit") ? PushMode::Gerrit
                         : mode == QLatin1StringView("github") ? PushMode::GitHub
                         : mode == QLatin1StringView("gitlab") ? PushMode::GitLab
                                                               : PushMode::Auto;
    if (m_pushSettings.mode == value)
        return;
    m_pushSettings.mode = value;
    emit pushSettingsChanged();
    refresh();
}

void Repository::setGerritBranch(const QString &branch)
{
    if (m_pushSettings.gerritBranch == branch.trimmed())
        return;
    m_pushSettings.gerritBranch = branch.trimmed();
    emit pushSettingsChanged();
    refresh();
}

void Repository::setGithubHosts(const QString &hosts)
{
    const QStringList list = splitReviewList(hosts);
    if (m_pushSettings.githubHosts == list)
        return;
    m_pushSettings.githubHosts = list;
    emit pushSettingsChanged();
    refresh();
}

void Repository::setGitlabHosts(const QString &hosts)
{
    const QStringList list = splitReviewList(hosts);
    if (m_pushSettings.gitlabHosts == list)
        return;
    m_pushSettings.gitlabHosts = list;
    emit pushSettingsChanged();
    refresh();
}

QDateTime Repository::lastFetch() const
{
    const QFileInfo fetchHead(m_gitDir + QStringLiteral("/FETCH_HEAD"));
    return !m_gitDir.isEmpty() && fetchHead.exists() ? fetchHead.lastModified() : QDateTime();
}

QString Repository::operation() const
{
    if (m_gitDir.isEmpty())
        return {};
    const QDir dir(m_gitDir);
    if (dir.exists(QStringLiteral("MERGE_HEAD")))
        return tr("Merging");
    if (dir.exists(QStringLiteral("rebase-merge")) || dir.exists(QStringLiteral("rebase-apply")))
        return tr("Rebasing");
    if (dir.exists(QStringLiteral("CHERRY_PICK_HEAD")))
        return tr("Cherry-picking");
    if (dir.exists(QStringLiteral("REVERT_HEAD")))
        return tr("Reverting");
    return {};
}

namespace {

// What git's check-ref-format refuses in a name of a ref, without running
// git on the UI thread
QString refFormatError(const QString &name)
{
    const auto tr = [](const char *text) { return QCoreApplication::translate("Repository", text); };
    if (name == QLatin1String("HEAD") || name == QLatin1String("@") || name.startsWith(QLatin1Char('-')))
        return tr("“%1” cannot be a name.").arg(name);
    static const QRegularExpression bad(QStringLiteral("[\\x00-\\x20\\x7f~^:?*\\[\\\\]"));
    if (const QRegularExpressionMatch m = bad.match(name); m.hasMatch()) {
        const QString what = m.captured(0) == QLatin1String(" ") ? tr("spaces") : QStringLiteral("“%1”").arg(m.captured(0));
        return tr("A name cannot have %1 in it.").arg(what);
    }
    if (name.contains(QLatin1String("..")) || name.contains(QLatin1String("@{")) || name.contains(QLatin1String("//")))
        return tr("A name cannot have “..”, “@{” or “//” in it.");
    if (name.endsWith(QLatin1Char('/')) || name.endsWith(QLatin1Char('.')))
        return tr("A name cannot end with “/” or “.”.");
    for (const QString &part : name.split(QLatin1Char('/'))) {
        if (part.startsWith(QLatin1Char('.')) || part.endsWith(QLatin1String(".lock")))
            return tr("No part of a name can start with “.” or end with “.lock”.");
    }
    return {};
}

} // namespace

QString Repository::tagNameError(const QString &name) const
{
    if (name.isEmpty())
        return tr("Give the tag a name.");
    if (const QString error = refFormatError(name); !error.isEmpty())
        return error;
    if (m_tags.contains(name))
        return tr("There is a tag %1 already.").arg(name);
    return {};
}

void Repository::createTag(const QString &name, const QString &sha, const QString &message)
{
    QStringList args{QStringLiteral("tag")};
    if (!message.trimmed().isEmpty())
        args += {QStringLiteral("-a"), QStringLiteral("-m"), message.trimmed()};
    args += {name, sha.isEmpty() ? QStringLiteral("HEAD") : sha};
    enqueueGitWrite(args, tr("Tagged %1.").arg(name), tr("Tagging %1 failed").arg(name));
}

void Repository::deleteTag(const QString &name)
{
    enqueueGitWrite({QStringLiteral("tag"), QStringLiteral("-d"), name}, tr("Deleted the tag %1.").arg(name),
                    tr("Deleting the tag %1 failed").arg(name));
}

void Repository::pushTag(const QString &name)
{
    if (m_plan) {
        emit errorOccurred(tr("Pushing is off while planning."));
        return;
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(false), name](const GitRunner &git) {
        Outcome outcome;
        QString remote = upstreamRemote(git);
        if (remote.isEmpty())
            remote = QStringLiteral("origin");
        const NetworkResult result = runNetwork(git, {QStringLiteral("push"), remote, QStringLiteral("refs/tags/") + name});
        if (result.ok)
            outcome.notice = tr("Pushed the tag %1 to %2.").arg(name, remote);
        else
            outcome.error = tr("Pushing the tag %1 failed: %2").arg(name, result.message);
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::enqueueNetwork(const QStringList &args, const QString &done, const QString &failed)
{
    if (m_plan) {
        emit errorOccurred(tr("That cannot be planned; end planning first."));
        return;
    }
    enqueue(JobKind::Write, [=, request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const NetworkResult result = runNetwork(git, args);
        if (result.ok)
            outcome.notice = done;
        else
            outcome.error = tr("%1: %2").arg(failed, result.message);
        outcome.links = result.links;
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

QString Repository::remoteOf(const QString &ref) const
{
    QString remote;
    for (const QVariant &entry : std::as_const(m_remoteList)) {
        const QString candidate = entry.toMap().value(QStringLiteral("name")).toString();
        if (ref.startsWith(candidate + QLatin1Char('/')) && candidate.size() > remote.size())
            remote = candidate;
    }
    return remote;
}

void Repository::pushAllTags(const QString &remote)
{
    QString to = remote;
    if (to.isEmpty())
        to = remoteOf(m_status.upstream);
    if (to.isEmpty())
        to = QStringLiteral("origin");
    enqueueNetwork({QStringLiteral("push"), to, QStringLiteral("--tags")}, tr("Pushed every tag to %1.").arg(to),
                   tr("Pushing the tags to %1 failed").arg(to));
}

void Repository::pushBranch(const QString &branch, const QString &remote, const QString &remoteBranch, bool track)
{
    if (branch.isEmpty() || remote.isEmpty() || remoteBranch.isEmpty())
        return;
    QStringList args{QStringLiteral("push")};
    if (track)
        args << QStringLiteral("--set-upstream");
    args << remote << QStringLiteral("refs/heads/%1:refs/heads/%2").arg(branch, remoteBranch);
    enqueueNetwork(args, tr("Pushed %1 to %2/%3.").arg(branch, remote, remoteBranch),
                   tr("Pushing %1 to %2 failed").arg(branch, remote));
}

void Repository::deleteRemoteBranch(const QString &name)
{
    const QString remote = remoteOf(name);
    if (remote.isEmpty()) {
        emit errorOccurred(tr("%1 is on no remote known here.").arg(name));
        return;
    }
    const QString branch = name.mid(remote.size() + 1);
    enqueueNetwork({QStringLiteral("push"), remote, QStringLiteral("--delete"), branch},
                   tr("Deleted %1 on %2.").arg(branch, remote), tr("Deleting %1 on %2 failed").arg(branch, remote));
}

bool Repository::hasSubmodules() const
{
    return valid() && QFile::exists(QDir(path()).filePath(QStringLiteral(".gitmodules")));
}

bool Repository::bisecting() const
{
    return !m_gitDir.isEmpty() && QFile::exists(QDir(m_gitDir).filePath(QStringLiteral("BISECT_START")));
}

QString Repository::cloneFolderName(const QString &url) const
{
    // Windows paths too, with backslashes
    QString name = url.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.endsWith(QLatin1Char('/')))
        name.chop(1);
    name = name.section(QLatin1Char('/'), -1).section(QLatin1Char(':'), -1);
    if (name.endsWith(QLatin1String(".git")))
        name.chop(4);
    return name;
}

void Repository::cloneRepository(const QString &url, const QString &parent, int depth, bool blobless)
{
    const QString folder = QDir(parent).filePath(cloneFolderName(url));
    if (m_plan)
        endPlan();
    QStringList args{QStringLiteral("clone")};
    if (depth > 0)
        args << QStringLiteral("--depth=%1").arg(depth);
    if (blobless)
        args << QStringLiteral("--filter=blob:none");
    args << QStringLiteral("--") << url << folder;
    enqueue(JobKind::Write, [url, folder, parent, args, history = freshHistoryRequest()](const GitRunner &) {
        Outcome outcome;
        const GitResult result = GitRunner(parent).run(args);
        if (!result.ok()) {
            outcome.error = tr("Cloning %1 failed: %2").arg(url, explainNetworkFailure(result.errorText().trimmed()));
            return outcome;
        }
        // As git names it, as opening does: on macOS the temporary folders
        // are under /private as well, and the clone known once
        const QString top = QString::fromUtf8(GitRunner(folder).run({QStringLiteral("rev-parse"),
                                                                     QStringLiteral("--show-toplevel")}).out).trimmed();
        outcome.openedPath = top.isEmpty() ? folder : top;
        outcome.workDir = outcome.openedPath;
        outcome.gitDir = QString::fromUtf8(GitRunner(outcome.openedPath).run(
            {QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")}).out).trimmed();
        outcome.notice = tr("Cloned %1 into %2.").arg(url, outcome.openedPath);
        outcome.state = loadState(sourceFor(GitRunner(outcome.openedPath), nullptr), {}, history);
        return outcome;
    });
}

void Repository::fetchMoreHistory(int commits)
{
    if (!m_shallow)
        return;
    const QStringList args{QStringLiteral("fetch"),
                           commits > 0 ? QStringLiteral("--deepen=%1").arg(commits) : QStringLiteral("--unshallow")};
    enqueueNetwork(args, commits > 0 ? tr("Fetched %n more commit(s) of the history.", "", commits)
                                     : tr("Fetched all of the history."),
                   tr("Fetching more history failed"));
    checkSetup();
}

void Repository::setSparseFolders(const QStringList &folders)
{
    QStringList clean;
    for (const QString &folder : folders) {
        const QString trimmed = folder.trimmed();
        if (!trimmed.isEmpty())
            clean << trimmed;
    }
    enqueueGitWrite(QStringList{QStringLiteral("sparse-checkout"), QStringLiteral("set"), QStringLiteral("--cone"),
                                QStringLiteral("--")} + clean,
                    clean.isEmpty() ? tr("Only the files at the top are checked out now.")
                                    : tr("Only %n folder(s) are checked out now, besides the files at the top.", "",
                                         int(clean.size())),
                    tr("Setting the sparse checkout failed"));
    checkSetup();
}

void Repository::disableSparseCheckout()
{
    enqueueGitWrite({QStringLiteral("sparse-checkout"), QStringLiteral("disable")},
                    tr("Every file is checked out again."), tr("Ending the sparse checkout failed"));
    checkSetup();
}

void Repository::loadSparseFolders()
{
    if (!valid())
        return;
    enqueue(JobKind::Read, [](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run({QStringLiteral("sparse-checkout"), QStringLiteral("list")});
        outcome.sparseFolders = result.ok() ? QString::fromUtf8(result.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                            : QStringList();
        return outcome;
    });
}

namespace {

// Which of the configured tools to start, as git difftool and mergetool
// pick them: --gui for a guitool; none at all, and git would ask on a
// terminal nobody sees
std::optional<QStringList> toolChoice(const GitRunner &git, bool merge)
{
    const auto configured = [&git](const QString &key) {
        return !git.run({QStringLiteral("config"), QStringLiteral("--get"), key}).out.trimmed().isEmpty();
    };
    if (configured(merge ? QStringLiteral("merge.guitool") : QStringLiteral("diff.guitool")))
        return QStringList{QStringLiteral("--gui")};
    if ((!merge && configured(QStringLiteral("diff.tool"))) || configured(QStringLiteral("merge.tool")))
        return QStringList();
    return std::nullopt;
}

} // namespace

void Repository::openDiffTool(const QString &path)
{
    if (!valid() || path.isEmpty())
        return;
    QStringList revs;
    switch (m_diffSource) {
    case UnstagedDiff:
        break;
    case StagedDiff:
        revs << QStringLiteral("--cached");
        break;
    case CommitDiff:
        revs << m_diffCommit;
        break;
    case CompareDiff:
        revs << (m_diffThreeDot ? m_diffBase + QStringLiteral("...") + m_diffCommit : m_diffBase)
             << (m_diffThreeDot ? QString() : m_diffCommit);
        break;
    case PatchSetDiff:
        revs << m_diffBase << m_diffCommit;
        break;
    default:
        emit errorOccurred(tr("There is no diff on display to open."));
        return;
    }
    revs.removeAll(QString());
    startDiffTool(path, revs, m_diffSource == CommitDiff);
}

void Repository::openChangeInDiffTool(const QString &path, bool staged)
{
    if (valid() && !path.isEmpty())
        startDiffTool(path, staged ? QStringList{QStringLiteral("--cached")} : QStringList(), false);
}

void Repository::startDiffTool(const QString &path, const QStringList &revs, bool commit)
{
    const QPointer<Repository> self(this);
    QThreadPool::globalInstance()->start([self, git = m_git, path, revs, commit] {
        QString error;
        if (const std::optional<QStringList> gui = toolChoice(git, false); !gui) {
            error = tr("No diff tool is set up: git config --global diff.tool meld, say, or kdiff3, vimdiff, "
                       "vscode.");
        } else {
            QStringList range = revs;
            // A commit against its parent, or against nothing for the first
            if (commit) {
                const QString parent = QString::fromUtf8(git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                                                  QStringLiteral("-q"), revs.first() + QStringLiteral("^")})
                                                             .out).trimmed();
                const QString empty = QString::fromUtf8(git.run({QStringLiteral("hash-object"), QStringLiteral("-t"),
                                                                 QStringLiteral("tree"), QStringLiteral("--stdin")})
                                                            .out).trimmed();
                range = {parent.isEmpty() ? empty : parent, revs.first()};
            }
            const GitResult result = git.run(QStringList{QStringLiteral("difftool"), QStringLiteral("-y")} + *gui
                                             + range + QStringList{QStringLiteral("--"), path});
            if (!result.ok())
                error = tr("The diff tool failed: %1").arg(result.errorText());
        }
        if (!error.isEmpty()) {
            QMetaObject::invokeMethod(qApp, [self, error] {
                if (self)
                    emit self->errorOccurred(error);
            });
        }
    });
}

void Repository::openMergeTool(const QString &path)
{
    if (!valid() || path.isEmpty())
        return;
    const QPointer<Repository> self(this);
    QThreadPool::globalInstance()->start([self, git = m_git, path] {
        QString error;
        if (const std::optional<QStringList> gui = toolChoice(git, true); !gui) {
            error = tr("No merge tool is set up: git config --global merge.tool meld, say, or kdiff3, vimdiff, "
                       "vscode.");
        } else {
            const GitResult result = git.run(QStringList{QStringLiteral("mergetool"), QStringLiteral("-y")} + *gui
                                             + QStringList{QStringLiteral("--"), path});
            if (!result.ok())
                error = tr("The merge tool did not resolve %1: %2").arg(path, result.errorText());
        }
        QMetaObject::invokeMethod(qApp, [self, error] {
            if (!self)
                return;
            if (!error.isEmpty())
                emit self->errorOccurred(error);
            self->refreshStatus();
        });
    });
}

void Repository::initRepository(const QString &folder)
{
    const QUrl url(folder);
    const QString path = url.isLocalFile() ? url.toLocalFile() : folder;
    if (m_plan)
        endPlan();
    enqueue(JobKind::Write, [path, history = freshHistoryRequest()](const GitRunner &) {
        Outcome outcome;
        QDir().mkpath(path);
        const GitResult result = GitRunner(path).run({QStringLiteral("init"), QStringLiteral("-q"), path});
        if (!result.ok()) {
            outcome.error = tr("Making a repository in %1 failed: %2").arg(path, result.errorText().trimmed());
            return outcome;
        }
        const QString top = QDir(path).canonicalPath();
        outcome.openedPath = top;
        outcome.workDir = top;
        outcome.gitDir = QString::fromUtf8(GitRunner(top).run(
            {QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")}).out).trimmed();
        outcome.notice = tr("Made a new repository in %1.").arg(top);
        outcome.state = loadState(sourceFor(GitRunner(top), nullptr), {}, history);
        return outcome;
    });
}

void Repository::addRemote(const QString &name, const QString &url)
{
    enqueueGitWrite({QStringLiteral("remote"), QStringLiteral("add"), name, url}, tr("Added the remote %1.").arg(name),
                    tr("Adding the remote %1 failed").arg(name));
}

void Repository::renameRemote(const QString &from, const QString &to)
{
    enqueueGitWrite({QStringLiteral("remote"), QStringLiteral("rename"), from, to},
                    tr("Renamed the remote %1 to %2.").arg(from, to), tr("Renaming the remote %1 failed").arg(from));
}

void Repository::setRemoteUrl(const QString &name, const QString &url)
{
    enqueueGitWrite({QStringLiteral("remote"), QStringLiteral("set-url"), name, url},
                    tr("%1 now points to %2.").arg(name, url), tr("Changing the remote %1 failed").arg(name));
}

void Repository::removeRemote(const QString &name)
{
    enqueueGitWrite({QStringLiteral("remote"), QStringLiteral("remove"), name}, tr("Removed the remote %1.").arg(name),
                    tr("Removing the remote %1 failed").arg(name));
}

void Repository::setUpstream(const QString &remoteBranch)
{
    enqueueGitWrite({QStringLiteral("branch"), QStringLiteral("--set-upstream-to=") + remoteBranch},
                    tr("%1 now tracks %2.").arg(m_status.branch, remoteBranch), tr("Setting the upstream failed"));
}

void Repository::addWorktree(const QString &path, const QString &branch, bool create, const QString &start)
{
    const QUrl url(path);
    const QString folder = url.isLocalFile() ? url.toLocalFile() : path;
    QStringList args{QStringLiteral("worktree"), QStringLiteral("add")};
    if (create)
        args += {QStringLiteral("-b"), branch, folder, start.isEmpty() ? QStringLiteral("HEAD") : start};
    else
        args += {folder, branch};
    enqueueGitWrite(args, tr("Made a worktree at %1, on %2.").arg(folder, branch), tr("Making the worktree failed"));
}

void Repository::removeWorktree(const QString &path)
{
    enqueueGitWrite({QStringLiteral("worktree"), QStringLiteral("remove"), path}, tr("Removed the worktree %1.").arg(path),
                    tr("Removing the worktree %1 failed").arg(path));
}

void Repository::updateSubmodules()
{
    if (m_plan) {
        emit errorOccurred(tr("Updating submodules is off while planning."));
        return;
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run({QStringLiteral("submodule"), QStringLiteral("update"), QStringLiteral("--init"),
                                          QStringLiteral("--recursive")});
        if (result.ok())
            outcome.notice = tr("The submodules are at the commits recorded.");
        else
            outcome.error = tr("Updating the submodules failed: %1").arg(explainNetworkFailure(result.errorText().trimmed()));
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::savePatch(const QString &sha, const QString &file)
{
    const QUrl url(file);
    const QString path = url.isLocalFile() ? url.toLocalFile() : file;
    enqueue(JobKind::Read, [sha, path](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run({QStringLiteral("format-patch"), QStringLiteral("-1"), QStringLiteral("--stdout"), sha});
        QFile out(path);
        if (!result.ok())
            outcome.error = tr("Saving the patch failed: %1").arg(result.errorText().trimmed());
        else if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(result.out) != result.out.size())
            outcome.error = tr("Writing %1 failed: %2").arg(path, out.errorString());
        else
            outcome.notice = tr("Saved %1 as %2.").arg(sha.left(10), path);
        return outcome;
    });
}

void Repository::applyPatch(const QString &file)
{
    const QUrl url(file);
    const QString path = url.isLocalFile() ? url.toLocalFile() : file;
    if (m_plan) {
        emit errorOccurred(tr("Applying patches is off while planning."));
        return;
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true), path](const GitRunner &git) {
        Outcome outcome;
        QFile in(path);
        if (!in.open(QIODevice::ReadOnly)) {
            outcome.error = tr("Reading %1 failed: %2").arg(path, in.errorString());
            return outcome;
        }
        // A commit as format-patch writes it starts with "From <sha>"
        const bool mail = in.peek(5) == "From ";
        in.close();
        // Not into an am already going on, which the abort below would undo
        const QString applying = QString::fromUtf8(git.run({QStringLiteral("rev-parse"), QStringLiteral("--git-path"),
                                                             QStringLiteral("rebase-apply")}).out).trimmed();
        if (mail && QDir(QDir(git.workDir()).filePath(applying)).exists()) {
            outcome.error = tr("A git am is going on already; finish or abort it first.");
            return outcome;
        }
        const GitResult result = mail ? git.run({QStringLiteral("am"), QStringLiteral("--3way"), path})
                                      : git.run({QStringLiteral("apply"), QStringLiteral("--whitespace=nowarn"), path});
        if (result.ok()) {
            outcome.notice = mail ? tr("Applied %1 as a commit.").arg(path) : tr("Applied %1 to the working tree.").arg(path);
        } else {
            // Nothing half done: an am that stopped is undone
            if (mail)
                git.run({QStringLiteral("am"), QStringLiteral("--abort")});
            outcome.error = tr("Applying %1 failed: %2").arg(path, result.errorText().trimmed());
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

namespace {

// What git bisect said last: how many are left, or the commit it found
QString bisectNews(const GitResult &result)
{
    // "<sha> is the first bad commit"; newer gits quote the term:
    // "is the first 'bad' commit"
    const QList<QByteArray> lines = (result.out + '\n' + result.err).trimmed().split('\n');
    for (const QByteArray &line : lines) {
        if (line.contains(" is the first "))
            return QString::fromUtf8(line);
    }
    return lines.isEmpty() ? QString() : QString::fromUtf8(lines.first());
}

} // namespace

void Repository::bisectMark(const QString &sha, const QString &verdict)
{
    if (verdict != QLatin1String("good") && verdict != QLatin1String("bad"))
        return;
    if (m_plan) {
        emit errorOccurred(tr("Bisecting is off while planning."));
        return;
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true), sha, verdict,
                             started = bisecting()](const GitRunner &git) {
        Outcome outcome;
        if (!started)
            git.run({QStringLiteral("bisect"), QStringLiteral("start")});
        const GitResult result = git.run({QStringLiteral("bisect"), verdict, sha});
        if (result.ok()) {
            const QString news = bisectNews(result);
            outcome.notice = news.isEmpty() ? tr("Marked %1 %2.").arg(sha.left(10), verdict) : news;
        }
        else
            outcome.error = tr("Bisecting: %1").arg(result.errorText().trimmed());
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::bisectCurrent(const QString &verdict)
{
    if (verdict != QLatin1String("good") && verdict != QLatin1String("bad") && verdict != QLatin1String("skip"))
        return;
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true), verdict](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run({QStringLiteral("bisect"), verdict});
        if (result.ok())
            outcome.notice = bisectNews(result);
        else
            outcome.error = tr("Bisecting: %1").arg(result.errorText().trimmed());
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::bisectReset()
{
    enqueueGitWrite({QStringLiteral("bisect"), QStringLiteral("reset")}, tr("Stopped bisecting; back where it began."),
                    tr("Stopping the bisection failed"));
}

void Repository::compareCommits(const QString &base, const QString &commit, const QString &baseName, bool threeDot)
{
    if (base.isEmpty() || commit.isEmpty() || !valid())
        return;
    setDiffView(CompareDiff, {}, commit);
    m_diffBase = base;
    m_diffBaseName = baseName;
    m_diffThreeDot = threeDot;
    emit diffChanged();
    enqueue(JobKind::Read, [request = diffRequest(), current = m_diffGeneration, plan = m_plan](const GitRunner &git) {
        Outcome outcome;
        if (request.generation == *current)
            outcome.state.diff = loadDiff(sourceFor(git, plan), request, {});
        return outcome;
    });
}

QString Repository::branchNameError(const QString &name) const
{
    if (name.isEmpty())
        return tr("Give the branch a name.");
    if (const QString error = refFormatError(name); !error.isEmpty())
        return error;
    if (m_branches.contains(name))
        return tr("There is a branch %1 already.").arg(name);
    // refs/heads/a cannot be both a branch and a folder of branches
    for (const QString &branch : m_branches) {
        if (branch.startsWith(name + QLatin1Char('/')) || name.startsWith(branch + QLatin1Char('/')))
            return tr("The branch %1 is in the way of that name.").arg(branch);
    }
    return {};
}

namespace {

// Continuing never stops at an editor: the message git prepared stands
const QStringList NoEditor{QStringLiteral("-c"), QStringLiteral("core.editor=true")};

// What git said, and that the operation waits for its conflicts, if it
// stopped at them
QString stoppedAt(const GitRunner &git, const GitResult &result)
{
    QString text = result.errorText().trimmed();
    if (text.isEmpty())
        text = QString::fromUtf8(result.out).trimmed();
    const QString operation = operationInProgress(git);
    if (!operation.isEmpty())
        return QCoreApplication::translate("Repository", "%1 stopped at conflicts: resolve them in the changes, "
                                                         "then continue, or abort.\n%2").arg(operation, text);
    return text;
}

} // namespace

void Repository::enqueueGitWrite(const QStringList &args, const QString &done, const QString &failed)
{
    if (m_plan) {
        emit errorOccurred(tr("That cannot be planned; end planning first."));
        return;
    }
    // The history again whatever HEAD does: a branch made elsewhere, or
    // renamed or deleted, shows only in its refs
    enqueue(JobKind::Write, [=, request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run(args);
        if (result.ok())
            outcome.notice = done;
        else
            outcome.error = tr("%1: %2").arg(failed, stoppedAt(git, result));
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::checkoutDetached(const QString &rev)
{
    if (rev.isEmpty())
        return;
    const QString shown = QRegularExpression(QStringLiteral("^[0-9a-f]{40,64}$")).match(rev).hasMatch()
                              ? rev.left(10) : rev;
    enqueueGitWrite({QStringLiteral("switch"), QStringLiteral("--detach"), rev},
                    tr("Checked out %1 on no branch; switch to a branch to commit on one.").arg(shown),
                    tr("Checking out %1").arg(shown));
}

void Repository::createBranch(const QString &name, const QString &start, bool checkout)
{
    const QString at = start.isEmpty() ? QStringLiteral("HEAD") : start;
    if (checkout)
        enqueueGitWrite({QStringLiteral("switch"), QStringLiteral("-c"), name, at},
                             tr("Switched to the new branch %1.").arg(name), tr("Making the branch %1 failed").arg(name));
    else
        enqueueGitWrite({QStringLiteral("branch"), name, at}, tr("Made the branch %1.").arg(name),
                             tr("Making the branch %1 failed").arg(name));
}

void Repository::checkoutBranch(const QString &name)
{
    // A remote-tracking branch: a local one of its name, tracking it
    if (m_remoteBranches.contains(name) && !m_branches.contains(name)) {
        const QString local = name.section(QLatin1Char('/'), 1);
        if (m_branches.contains(local)) {
            enqueueGitWrite({QStringLiteral("switch"), local}, tr("Switched to %1.").arg(local),
                                 tr("Switching to %1 failed").arg(local));
            return;
        }
        enqueueGitWrite({QStringLiteral("switch"), QStringLiteral("-c"), local, QStringLiteral("--track"), name},
                             tr("Switched to the new branch %1, which tracks %2.").arg(local, name),
                             tr("Switching to %1 failed").arg(name));
        return;
    }
    enqueueGitWrite({QStringLiteral("switch"), name}, tr("Switched to %1.").arg(name),
                         tr("Switching to %1 failed").arg(name));
}

void Repository::renameBranch(const QString &from, const QString &to)
{
    enqueueGitWrite({QStringLiteral("branch"), QStringLiteral("-m"), from, to},
                         tr("Renamed %1 to %2.").arg(from, to), tr("Renaming %1 failed").arg(from));
}

void Repository::deleteBranch(const QString &name, bool force)
{
    enqueueGitWrite({QStringLiteral("branch"), force ? QStringLiteral("-D") : QStringLiteral("-d"), name},
                         tr("Deleted the branch %1.").arg(name), tr("Deleting %1 failed").arg(name));
}


void Repository::pull(const QString &how, bool autostash)
{
    if (m_plan) {
        emit errorOccurred(tr("Pulling is off while planning."));
        return;
    }
    if (m_status.upstream.isEmpty()) {
        emit errorOccurred(tr("%1 has no upstream to pull from.").arg(m_status.branch));
        return;
    }
    const QString upstream = m_status.upstream;
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true), upstream, how,
                             autostash](const GitRunner &git) {
        Outcome outcome;
        const NetworkResult pulled = pullUpstream(git, how, autostash);
        if (pulled.ok)
            outcome.notice = tr("Pulled %1.").arg(upstream);
        else if (!operationInProgress(git).isEmpty())
            outcome.error = tr("Pulling %1: %2").arg(upstream, tr("%1 stopped at conflicts: resolve them in the changes, "
                                                                  "then continue, or abort.\n%2")
                                                                   .arg(operationInProgress(git), pulled.message));
        else
            outcome.error = tr("Pulling %1 failed: %2").arg(upstream, pulled.message);
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::mergeBranch(const QString &name, const QString &how)
{
    QStringList args{QStringLiteral("merge"), QStringLiteral("--no-edit")};
    QString done = tr("Merged %1.").arg(name);
    if (how == QLatin1String("no-ff")) {
        args << QStringLiteral("--no-ff");
    } else if (how == QLatin1String("ff-only")) {
        args << QStringLiteral("--ff-only");
    } else if (how == QLatin1String("squash")) {
        args << QStringLiteral("--squash");
        done = tr("Staged what %1 changes, for a commit of its own.").arg(name);
    }
    enqueueGitWrite(args + QStringList{name}, done, tr("Merging %1").arg(name));
}

void Repository::rebaseOnto(const QString &name, const QString &after)
{
    if (after.isEmpty()) {
        enqueueGitWrite({QStringLiteral("rebase"), name}, tr("Rebased onto %1.").arg(name),
                        tr("Rebasing onto %1").arg(name));
        return;
    }
    enqueueGitWrite({QStringLiteral("rebase"), QStringLiteral("--onto"), name, after},
                    tr("Moved the commits after %1 onto %2.").arg(after, name),
                    tr("Moving the commits after %1 onto %2").arg(after, name));
}

void Repository::continueOperation()
{
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const QString operation = operationInProgress(git);
        const QString command = operationCommand(git);
        // A merge is continued by committing it
        QStringList args;
        if (command == QLatin1String("merge"))
            args = {QStringLiteral("commit"), QStringLiteral("--no-edit")};
        else if (!command.isEmpty())
            args = NoEditor + QStringList{command, QStringLiteral("--continue")};
        if (args.isEmpty()) {
            outcome.error = tr("Nothing is waiting to be continued.");
        } else {
            const GitResult result = git.run(args);
            if (result.ok())
                outcome.notice = operationInProgress(git).isEmpty() ? tr("%1 finished.").arg(operation)
                                                                    : tr("%1 goes on.").arg(operation);
            else
                outcome.error = tr("%1: %2").arg(operation, stoppedAt(git, result));
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::abortOperation()
{
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const QString operation = operationInProgress(git);
        const QString command = operationCommand(git);
        if (command.isEmpty()) {
            outcome.error = tr("Nothing is waiting to be aborted.");
        } else {
            const GitResult result = git.run({command, QStringLiteral("--abort")});
            if (result.ok())
                outcome.notice = tr("%1 aborted; everything is as before.").arg(operation);
            else
                outcome.error = tr("Aborting failed: %1").arg(result.errorText().trimmed());
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

namespace {

// A path as a literal gitignore pattern: wildcards and a leading # or !
// escaped, as are trailing spaces, which git would drop
QString ignoreLiteral(const QString &text)
{
    QString escaped;
    for (const QChar c : text) {
        if (c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('[') || c == QLatin1Char('\\'))
            escaped += QLatin1Char('\\');
        escaped += c;
    }
    if (escaped.startsWith(QLatin1Char('#')) || escaped.startsWith(QLatin1Char('!')))
        escaped.prepend(QLatin1Char('\\'));
    if (escaped.endsWith(QLatin1Char(' ')))
        escaped.insert(escaped.size() - 1, QLatin1Char('\\'));
    return escaped;
}

} // namespace

QStringList Repository::ignorePatterns(const QString &path) const
{
    const bool folder = path.endsWith(QLatin1Char('/'));
    const QString bare = folder ? path.chopped(1) : path;
    if (bare.isEmpty())
        return {};
    const qsizetype slash = bare.lastIndexOf(QLatin1Char('/'));
    const QString name = bare.mid(slash + 1);
    QStringList patterns{QLatin1Char('/') + ignoreLiteral(bare) + (folder ? QStringLiteral("/") : QString())};
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (!folder && dot > 0)
        patterns << QLatin1Char('*') + ignoreLiteral(name.mid(dot));
    if (slash > 0)
        patterns << QLatin1Char('/') + ignoreLiteral(bare.left(slash)) + QLatin1Char('/');
    if (slash > 0)
        patterns << ignoreLiteral(name) + (folder ? QStringLiteral("/") : QString());
    return patterns;
}

void Repository::ignore(const QString &pattern, bool local)
{
    if (pattern.trimmed().isEmpty() || !valid())
        return;
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(false), pattern,
                             local](const GitRunner &git) {
        Outcome outcome;
        QString file = QDir(git.workDir()).filePath(QStringLiteral(".gitignore"));
        if (local) {
            const QString exclude = QString::fromUtf8(git.run({QStringLiteral("rev-parse"), QStringLiteral("--git-path"),
                                                               QStringLiteral("info/exclude")}).out).trimmed();
            file = QDir(git.workDir()).absoluteFilePath(exclude);
            QDir().mkpath(QFileInfo(file).absolutePath());
        }
        QFile out(file);
        QByteArray before;
        if (out.open(QIODevice::ReadOnly)) {
            before = out.readAll();
            out.close();
        }
        if (!out.open(QIODevice::Append)) {
            outcome.error = tr("Could not write %1: %2").arg(file, out.errorString());
            return outcome;
        }
        if (!before.isEmpty() && !before.endsWith('\n'))
            out.write("\n");
        out.write(pattern.toUtf8() + '\n');
        out.close();
        outcome.notice = local ? tr("Ignoring %1 in this clone (info/exclude).").arg(pattern)
                               : tr("Ignoring %1 (.gitignore).").arg(pattern);
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::discardFiles(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    if (paths.size() == 1) {
        discardFile(paths.first());
        return;
    }
    if (m_plan) {
        emit errorOccurred(tr("Discarding is off while planning."));
        return;
    }
    QStringList tracked;
    QStringList untracked;
    for (const QString &path : paths) {
        bool isUntracked = false;
        for (const StatusEntry &entry : std::as_const(m_status.entries)) {
            if (entry.path == path)
                isUntracked = entry.untracked;
        }
        (isUntracked ? untracked : tracked).append(path);
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(false), tracked,
                             untracked](const GitRunner &git) {
        Outcome outcome;
        QStringList failures;
        if (!tracked.isEmpty()) {
            const GitResult result = git.run(QStringList{QStringLiteral("restore"), QStringLiteral("--worktree"),
                                                         QStringLiteral("--")} + tracked);
            if (!result.ok())
                failures << result.errorText();
        }
        if (!untracked.isEmpty()) {
            const GitResult result = git.run(QStringList{QStringLiteral("clean"), QStringLiteral("-f"),
                                                         QStringLiteral("-q"), QStringLiteral("--")} + untracked);
            if (!result.ok())
                failures << result.errorText();
        }
        if (failures.isEmpty())
            outcome.notice = tr("Discarded the changes to %n file(s).", "", int(tracked.size() + untracked.size()));
        else
            outcome.error = tr("Discarding failed: %1").arg(failures.join(QLatin1Char('\n')));
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::previewClean(bool ignored)
{
    if (!valid())
        return;
    enqueue(JobKind::Read, [ignored](const GitRunner &git) {
        Outcome outcome;
        QStringList args{QStringLiteral("clean"), QStringLiteral("-n"), QStringLiteral("-d")};
        if (ignored)
            args << QStringLiteral("-x");
        const GitResult result = git.run(args);
        QStringList paths;
        for (const QByteArray &line : result.out.split('\n')) {
            if (line.startsWith("Would remove "))
                paths << QString::fromUtf8(line.mid(13));
        }
        outcome.cleanPreview = std::pair{paths, ignored};
        return outcome;
    });
}

void Repository::clean(const QStringList &paths, bool ignored)
{
    if (paths.isEmpty())
        return;
    QStringList args{QStringLiteral("clean"), QStringLiteral("-f"), QStringLiteral("-d"), QStringLiteral("-q")};
    if (ignored)
        args << QStringLiteral("-x");
    enqueueGitWrite(args + QStringList{QStringLiteral("--")} + paths,
                    tr("Deleted %n untracked file(s) and folder(s).", "", int(paths.size())),
                    tr("Cleaning failed"));
}

void Repository::discardFile(const QString &path)
{
    bool untracked = false;
    for (const StatusEntry &entry : std::as_const(m_status.entries)) {
        if (entry.path == path)
            untracked = entry.untracked;
    }
    if (untracked)
        enqueueGitWrite({QStringLiteral("clean"), QStringLiteral("-f"), QStringLiteral("-q"), QStringLiteral("--"), path},
                        tr("Deleted %1.").arg(path), tr("Deleting %1 failed").arg(path));
    else
        enqueueGitWrite({QStringLiteral("restore"), QStringLiteral("--worktree"), QStringLiteral("--"), path},
                        tr("Discarded the changes to %1.").arg(path), tr("Discarding the changes to %1 failed").arg(path));
}

void Repository::discardSelection()
{
    if (m_diffSource != UnstagedDiff || m_ignoreWhitespace)
        return;
    const int lines = m_diff.selectedCount();
    if (m_plan) {
        emit errorOccurred(tr("Discarding is off while planning."));
        return;
    }
    // An untracked file has no other side to apply a patch back to: its
    // selected lines are taken out of it
    bool untracked = false;
    for (const StatusEntry &entry : std::as_const(m_status.entries)) {
        if (entry.path == m_diffPath)
            untracked = entry.untracked;
    }
    if (untracked && m_diff.files().size() == 1) {
        QSet<int> drop; // 1-based line numbers of the file
        const FileDiff &file = m_diff.files().first();
        const LineSelection selection = m_diff.selection(0);
        for (int h = 0; h < file.hunks.size() && h < selection.size(); ++h) {
            const DiffHunk &hunk = file.hunks[h];
            for (int i = 0; i < hunk.lines.size() && i < selection[h].size(); ++i) {
                if (selection[h][i] && hunk.lines[i].kind == DiffLine::Added)
                    drop.insert(hunk.lines[i].newLine);
            }
        }
        if (drop.isEmpty())
            return;
        const QString path = QDir(this->path()).filePath(m_diffPath);
        enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(false), path, drop, lines](const GitRunner &git) {
            Outcome outcome;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                outcome.error = tr("Reading %1 failed: %2").arg(path, file.errorString());
                return outcome;
            }
            const QByteArray content = file.readAll();
            file.close();
            QList<QByteArray> kept;
            const QList<QByteArray> all = content.split('\n');
            // The last part after a final newline is not a line
            const qsizetype count = content.endsWith('\n') ? all.size() - 1 : all.size();
            for (qsizetype i = 0; i < count; ++i) {
                if (!drop.contains(int(i) + 1))
                    kept.append(all[i]);
            }
            QByteArray out = kept.join('\n');
            if (!kept.isEmpty() && content.endsWith('\n'))
                out += '\n';
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(out) != out.size())
                outcome.error = tr("Writing %1 failed: %2").arg(path, file.errorString());
            else
                outcome.notice = tr("Discarded %n line(s).", "", lines);
            outcome.state = loadState(sourceFor(git, nullptr), request, history);
            return outcome;
        });
        return;
    }
    // The selection taken out of the working tree: built as for unstaging,
    // with the rest of the working tree's side intact, and applied back
    const QByteArray patch = selectionPatch(PatchDirection::Reverse);
    if (patch.isEmpty())
        return;
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(false), patch, lines](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run({QStringLiteral("apply"), QStringLiteral("--reverse"),
                                          QStringLiteral("--whitespace=nowarn")}, patch);
        if (result.ok())
            outcome.notice = tr("Discarded %n line(s).", "", lines);
        else
            outcome.error = tr("Discarding lines failed: %1").arg(result.errorText().trimmed());
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::stash(const QString &message, bool includeUntracked, bool stagedOnly, const QStringList &paths)
{
    QStringList args{QStringLiteral("stash"), QStringLiteral("push")};
    // git takes untracked files or only the staged changes, not both
    if (stagedOnly)
        args.append(QStringLiteral("--staged"));
    else if (includeUntracked)
        args.append(QStringLiteral("--include-untracked"));
    if (!message.trimmed().isEmpty())
        args += {QStringLiteral("-m"), message.trimmed()};
    QString done = stagedOnly ? tr("Stashed the staged changes.") : tr("Stashed the changes.");
    if (!paths.isEmpty()) {
        args += QStringList{QStringLiteral("--")} + paths;
        done = paths.size() == 1 ? tr("Stashed the changes to %1.").arg(paths.first())
                                 : tr("Stashed the changes to %n file(s).", "", int(paths.size()));
    }
    enqueueGitWrite(args, done, tr("Stashing failed"));
}

void Repository::applyStash(int index, bool pop)
{
    const QString ref = QStringLiteral("stash@{%1}").arg(index);
    enqueueGitWrite({QStringLiteral("stash"), pop ? QStringLiteral("pop") : QStringLiteral("apply"), ref},
                    pop ? tr("Applied and dropped %1.").arg(ref) : tr("Applied %1; it is kept.").arg(ref),
                    tr("Applying %1").arg(ref));
}

void Repository::checkSetup()
{
    enqueue(JobKind::Read, [](const GitRunner &git) {
        Outcome outcome;
        // Any file the attributes give the LFS filter, in any .gitattributes
        const GitResult files = git.run({QStringLiteral("ls-files"), QStringLiteral(":(attr:filter=lfs)")});
        const bool used = files.ok() && !files.out.trimmed().isEmpty();
        const bool installed = used && git.run({QStringLiteral("lfs"), QStringLiteral("version")}).ok();
        outcome.lfs = std::pair{used, installed};
        // A path relative to the work tree's top, as git commit reads it
        const QString file = QString::fromUtf8(git.run({QStringLiteral("config"), QStringLiteral("--path"),
                                                        QStringLiteral("commit.template")}).out).trimmed();
        QString text;
        if (!file.isEmpty()) {
            QFile in(QDir(git.workDir()).absoluteFilePath(file));
            if (in.open(QIODevice::ReadOnly))
                text = QString::fromUtf8(git.run({QStringLiteral("stripspace"), QStringLiteral("--strip-comments")},
                                                 in.readAll()).out);
        }
        outcome.commitTemplate = text;
        const bool shallow = git.run({QStringLiteral("rev-parse"), QStringLiteral("--is-shallow-repository")})
                                 .out.trimmed() == "true";
        const bool sparse = git.run({QStringLiteral("config"), QStringLiteral("--type=bool"),
                                     QStringLiteral("core.sparseCheckout")}).out.trimmed() == "true";
        outcome.layout = std::pair{shallow, sparse};
        return outcome;
    });
}

void Repository::lockFile(const QString &path, bool lock, bool force)
{
    if (!m_lfsInstalled || path.isEmpty())
        return;
    enqueue(JobKind::Write, [path, lock, force](const GitRunner &git) {
        Outcome outcome;
        QStringList args{QStringLiteral("lfs"), lock ? QStringLiteral("lock") : QStringLiteral("unlock")};
        if (!lock && force)
            args << QStringLiteral("--force");
        const GitResult result = git.run(args + QStringList{path});
        if (result.ok())
            outcome.notice = lock ? tr("Locked %1.").arg(path) : tr("Unlocked %1.").arg(path);
        else
            outcome.error = (lock ? tr("Locking %1: %2") : tr("Unlocking %1: %2")).arg(path, result.errorText());
        return outcome;
    });
}

void Repository::findLostWork()
{
    if (!valid())
        return;
    enqueue(JobKind::Read, [](const GitRunner &git) {
        Outcome outcome;
        QVariantList commits;
        for (const LostCommit &commit : ::findLostWork(git)) {
            commits.append(QVariantMap{
                {QStringLiteral("sha"), commit.sha},
                {QStringLiteral("subject"), commit.subject},
                {QStringLiteral("time"), commit.time},
                {QStringLiteral("kind"), commit.kind == LostCommit::Reflog ? QStringLiteral("reflog")
                                                                           : QStringLiteral("dangling")},
                {QStringLiteral("selector"), commit.selector},
                {QStringLiteral("action"), commit.action},
                {QStringLiteral("unreachable"), commit.unreachable},
                {QStringLiteral("stash"), commit.stash},
            });
        }
        outcome.lostWork = commits;
        return outcome;
    });
}

void Repository::restoreStash(const QString &sha, const QString &subject)
{
    enqueueGitWrite({QStringLiteral("stash"), QStringLiteral("store"), QStringLiteral("-m"), subject, sha},
                    tr("%1 is back among the stashes, as stash@{0}.").arg(sha.left(10)),
                    tr("Restoring the stash %1").arg(sha.left(10)));
}

void Repository::dropStash(int index)
{
    const QString ref = QStringLiteral("stash@{%1}").arg(index);
    enqueueGitWrite({QStringLiteral("stash"), QStringLiteral("drop"), ref}, tr("Dropped %1.").arg(ref),
                    tr("Dropping %1 failed").arg(ref));
}

namespace {

// For a merge commit, the side to take: its first parent's
QStringList mainline(const GitRunner &git, const QString &sha)
{
    const QList<QByteArray> ids = git.run({QStringLiteral("rev-list"), QStringLiteral("--parents"),
                                           QStringLiteral("-n1"), sha}).out.trimmed().split(' ');
    return ids.size() > 2 ? QStringList{QStringLiteral("-m"), QStringLiteral("1")} : QStringList();
}

} // namespace

void Repository::cherryPick(const QString &sha)
{
    if (m_plan) {
        emit errorOccurred(tr("Cherry-picking is off while planning."));
        return;
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true), sha](const GitRunner &git) {
        Outcome outcome;
        // A commit the branch has already: nothing to pick
        if (git.run({QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), sha, QStringLiteral("HEAD")}).ok()) {
            outcome.error = tr("The branch has %1 already.").arg(sha.left(10));
            return outcome;
        }
        const GitResult result = git.run(QStringList{QStringLiteral("cherry-pick")} + mainline(git, sha) + QStringList{sha});
        const QString said = QString::fromUtf8(result.out) + result.errorText();
        if (result.ok()) {
            outcome.notice = tr("Picked %1.").arg(sha.left(10));
        } else if (said.contains(QLatin1String("is now empty"))) {
            // Its change is in the branch by another commit: not left open
            git.run({QStringLiteral("cherry-pick"), QStringLiteral("--skip")});
            outcome.error = tr("The branch has what %1 changes already; nothing was picked.").arg(sha.left(10));
        } else {
            outcome.error = tr("Cherry-picking %1: %2").arg(sha.left(10), stoppedAt(git, result));
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::cherryPickCommits(const QStringList &shas)
{
    if (shas.size() == 1) {
        cherryPick(shas.first());
        return;
    }
    if (m_plan) {
        emit errorOccurred(tr("Cherry-picking is off while planning."));
        return;
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true), shas](const GitRunner &git) {
        Outcome outcome;
        QStringList picks;
        for (const QString &sha : shas) {
            if (!mainline(git, sha).isEmpty()) {
                outcome.error = tr("%1 is a merge; pick merges one at a time.").arg(sha.left(10));
                return outcome;
            }
            if (!git.run({QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), sha, QStringLiteral("HEAD")}).ok())
                picks << sha;
        }
        if (picks.isEmpty()) {
            outcome.error = tr("The branch has all of them already.");
            return outcome;
        }
        GitResult result = git.run(QStringList{QStringLiteral("cherry-pick")} + picks);
        // One whose change the branch has by another commit is skipped,
        // and the rest picked on
        int skipped = 0;
        while (!result.ok() && (QString::fromUtf8(result.out) + result.errorText()).contains(QLatin1String("is now empty"))) {
            ++skipped;
            result = git.run({QStringLiteral("cherry-pick"), QStringLiteral("--skip")});
        }
        const int had = int(shas.size() - picks.size()) + skipped;
        if (result.ok())
            outcome.notice = had == 0 ? tr("Picked %n commit(s).", "", int(picks.size()))
                                      : tr("Picked %1; %2 the branch had already.")
                                            .arg(tr("%n commit(s)", "", int(picks.size()) - skipped))
                                            .arg(had);
        else
            outcome.error = tr("Cherry-picking: %1").arg(stoppedAt(git, result));
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::revertCommit(const QString &sha)
{
    if (m_plan) {
        emit errorOccurred(tr("Reverting is off while planning."));
        return;
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true), sha](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run(QStringList{QStringLiteral("revert"), QStringLiteral("--no-edit")}
                                         + mainline(git, sha) + QStringList{sha});
        if (result.ok())
            outcome.notice = tr("Reverted %1 with a new commit.").arg(sha.left(10));
        else
            outcome.error = tr("Reverting %1: %2").arg(sha.left(10), stoppedAt(git, result));
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::resetTo(const QString &sha, const QString &mode)
{
    if (mode != QLatin1String("soft") && mode != QLatin1String("mixed") && mode != QLatin1String("hard"))
        return;
    enqueueGitWrite({QStringLiteral("reset"), QStringLiteral("--") + mode, sha},
                    tr("Reset %1 to %2.").arg(m_status.branch.isEmpty() ? QStringLiteral("HEAD") : m_status.branch,
                                              sha.left(10)),
                    tr("Resetting failed"));
}

void Repository::setAutoFetchMinutes(int minutes)
{
    minutes = std::max(0, minutes);
    if (minutes == m_autoFetchMinutes)
        return;
    m_autoFetchMinutes = minutes;
    // Checked every minute: a fetch is due by the last one's age, which a
    // fetch from a terminal resets too
    if (minutes > 0)
        m_autoFetchTimer.start(60 * 1000);
    else
        m_autoFetchTimer.stop();
    emit autoFetchMinutesChanged();
}

void Repository::fetchInBackground()
{
    if (!valid() || m_plan || m_autoFetchMinutes <= 0 || m_pendingJobs > 0 || !operation().isEmpty()
        || m_remoteList.isEmpty())
        return;
    const QDateTime last = lastFetch();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTime()) < m_autoFetchMinutes * 60)
        return;
    enqueue(JobKind::Read, [request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        // Quiet: a failure waits for the next fetch asked for
        fetchFrom(git, upstreamRemote(git), true);
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::fetch()
{
    if (m_plan) {
        emit errorOccurred(tr("Fetching is off while planning."));
        return;
    }
    enqueue(JobKind::Write, [request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        const QString remote = upstreamRemote(git);
        const NetworkResult result = fetchFrom(git, remote);
        if (result.ok)
            outcome.notice = remote.isEmpty() ? tr("Fetched all remotes.") : tr("Fetched %1.").arg(remote);
        else
            outcome.error = tr("Fetching failed: %1").arg(result.message);
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

namespace {

std::optional<ReviewOptions> reviewOptions(const QVariantMap &map)
{
    if (map.isEmpty())
        return std::nullopt;
    ReviewOptions options;
    options.topic = map.value(QStringLiteral("topic")).toString().trimmed();
    const QString state = map.value(QStringLiteral("state")).toString();
    options.state = state == QLatin1StringView("wip")     ? ReviewOptions::State::Wip
                  : state == QLatin1StringView("ready") ? ReviewOptions::State::Ready
                                                        : ReviewOptions::State::Unchanged;
    options.reviewers = splitReviewList(map.value(QStringLiteral("reviewers")).toString());
    options.hashtags = splitReviewList(map.value(QStringLiteral("hashtags")).toString());
    options.create = map.value(QStringLiteral("create")).toBool();
    options.target = map.value(QStringLiteral("target")).toString().trimmed();
    options.title = map.value(QStringLiteral("title")).toString().trimmed();
    // Labels may have spaces.
    for (const QString &label : map.value(QStringLiteral("labels")).toString().split(QLatin1Char(','))) {
        if (!label.trimmed().isEmpty())
            options.labels.append(label.trimmed());
    }
    options.removeSourceBranch = map.value(QStringLiteral("removeSourceBranch")).toBool();
    return options;
}

} // namespace

QVariantMap Repository::pushPreview(const QVariantMap &review) const
{
    const ReviewProvider *provider = reviewProvider(m_pushTarget.host);
    if (!provider)
        return {{QStringLiteral("text"), QString()}, {QStringLiteral("error"), m_pushTarget.error}};
    // Options that cannot be sent are the first thing to fix.
    PushTarget target = m_pushTarget;
    target.error.clear();
    provider->applyOptions(target, reviewOptions(review).value_or(ReviewOptions{}));
    if (target.error.isEmpty())
        target.error = m_pushTarget.error;
    return {{QStringLiteral("text"), target.error.isEmpty() ? provider->preview(target) : QString()},
            {QStringLiteral("error"), target.error}};
}

void Repository::push(bool force, const QVariantMap &review)
{
    if (m_plan) {
        emit errorOccurred(tr("Pushing is off while planning; execute or discard the plan first."));
        return;
    }
    if (const QString op = operation(); !op.isEmpty()) {
        emit errorOccurred(tr("git is in the middle of an operation (%1); finish or abort it first.").arg(op));
        return;
    }
    enqueue(JobKind::Write, [force, settings = m_pushSettings, review = reviewOptions(review),
                             request = diffRequest(), history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        // Resolved again: the branch or its configuration may have changed.
        const PushTarget target = resolvePushTarget(git, settings, review);
        const NetworkResult result = pushTo(git, target, force);
        if (result.ok) {
            outcome.notice = target.forReview ? tr("Pushed for review to refs/for/%1.").arg(target.remoteBranch)
                                              : tr("Pushed to %1/%2.").arg(target.remote, target.remoteBranch);
            outcome.links = result.links;
            outcome.error = result.warning;
        } else if (result.rejected && !target.forReview && !force) {
            outcome.pushRejected = result.message;
        } else {
            outcome.error = tr("Pushing failed: %1").arg(result.message);
        }
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
        return outcome;
    });
}

void Repository::forgetFold()
{
    if (!canUndoFold())
        return;
    m_lastFold = {};
    emit canUndoFoldChanged();
}

void Repository::updateFoldSuggestion()
{
    QString target;
    QString text;
    if ((m_diffSource == UnstagedDiff || m_diffSource == StagedDiff) && m_diff.selectedCount() > 0) {
        QList<LineSelection> selection;
        for (int f = 0; f < m_diff.files().size(); ++f)
            selection.append(m_diff.selection(f));
        const FoldSuggestion suggestion = m_suggester.suggest(selection);
        switch (suggestion.status) {
        case FoldSuggestion::Single:
            target = suggestion.target;
            text = tr("Belongs to %1").arg(commitLabel(target));
            break;
        case FoldSuggestion::Ambiguous: {
            QStringList labels;
            for (const QString &sha : suggestion.targets)
                labels.append(commitLabel(sha));
            text = tr("The selection belongs to %n commits: %1", "", int(labels.size()))
                       .arg(labels.join(QStringLiteral(", ")));
            break;
        }
        case FoldSuggestion::None:
            text = suggestion.reason;
            break;
        }
    }
    if (target == m_suggestedFoldTarget && text == m_foldSuggestionText)
        return;
    m_suggestedFoldTarget = target;
    m_foldSuggestionText = text;
    emit foldSuggestionChanged();
}

QString Repository::commitLabel(const QString &sha) const
{
    const int row = m_commits.rowOf(sha);
    if (row < 0)
        return sha.left(10);
    return QStringLiteral("“%1”").arg(m_commits.data(m_commits.index(row), CommitModel::SubjectRole).toString());
}

bool Repository::isMergeCommit(const QString &sha) const
{
    const int row = m_commits.rowOf(sha);
    return row >= 0 && m_commits.data(m_commits.index(row), CommitModel::IsMergeRole).toBool();
}

QByteArray Repository::selectionPatch(PatchDirection direction) const
{
    QByteArray patch;
    for (int f = 0; f < m_diff.files().size(); ++f)
        patch += buildPatch(m_diff.files()[f], m_diff.selection(f), direction);
    return patch;
}
