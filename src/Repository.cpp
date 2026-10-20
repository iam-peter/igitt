#include "Repository.h"

#include "core/FoldEngine.h"
#include "core/PatchBuilder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace {

DiffKind diffKind(Repository::DiffSource source)
{
    switch (source) {
    case Repository::UnstagedDiff: return DiffKind::Unstaged;
    case Repository::StagedDiff: return DiffKind::Staged;
    case Repository::CommitDiff: return DiffKind::Commit;
    case Repository::NoDiff: break;
    }
    return DiffKind::None;
}

} // namespace

Repository::Repository(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    connect(&m_diff, &DiffModel::selectionChanged, this, &Repository::updateFoldSuggestion);
    // Rows request their stats as they scroll into view; collect a batch.
    m_statsTimer.setSingleShot(true);
    m_statsTimer.setInterval(30);
    connect(&m_statsTimer, &QTimer::timeout, this, &Repository::loadRequestedStats);
}

Repository::~Repository()
{
    // Jobs only hold copies; wait so none posts back into a dead object.
    m_pool.clear();
    m_pool.waitForDone();
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
        m_commitDetails.clear();
        ++*m_diffGeneration;
        m_suggester = FoldSuggester();
        m_diff.clear();
        emit pathChanged();
        emit diffChanged();
        emit canUndoFoldChanged();
    }

    // Results for a repository that has been replaced meanwhile, or read
    // in or out of plan mode before it was switched, are of no use.
    if (outcome.workDir == m_git.workDir() && outcome.planGeneration == m_planGeneration) {
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
        if (!outcome.pushRejected.isEmpty())
            emit pushRejected(outcome.pushRejected, m_pushTarget.description());
        if (!outcome.notice.isEmpty())
            emit notice(outcome.notice);
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
    }
    if (state.history) {
        const HistoryState &history = *state.history;
        m_historyHead = history.head;
        m_historyWip = history.wip;
        m_pushTarget = history.pushTarget;
        m_wipGraph = history.wipGraph;
        m_headMessage = history.headMessage;
        m_foldTargets = QSet<QString>(history.foldTargets.cbegin(), history.foldTargets.cend());
        m_unpublished = history.unpublished;
        m_commits.setCommits(history.commits, history.graph, history.refs, history.head, m_status.branch,
                             history.planned);
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
        else
            m_diff.setFiles(diff.files);
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
    enqueue(JobKind::Read, [path, history = historyRequest(true)](const GitRunner &) {
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

void Repository::showWorkingTree()
{
    if (m_unstaged.count() > 0)
        showFile(m_unstaged.data(m_unstaged.index(0), FileListModel::PathRole).toString(), false);
    else if (m_staged.count() > 0)
        showFile(m_staged.data(m_staged.index(0), FileListModel::PathRole).toString(), true);
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
    // Follow the file to the side it moves to.
    if (m_diffSource == UnstagedDiff && m_diffPath == path)
        setDiffView(StagedDiff, path, {});
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::StagePaths;
        entry.paths = {path};
        entry.title = tr("Stage %1").arg(path);
        enqueuePlanEntry(entry);
        return;
    }
    enqueueCommand(tr("Staging %1").arg(path), {QStringLiteral("add"), QStringLiteral("--"), path});
}

void Repository::unstageFile(const QString &path)
{
    QStringList paths{path};
    if (const StatusEntry *entry = m_staged.entry(path); entry && !entry->origPath.isEmpty())
        paths.append(entry->origPath);
    if (m_diffSource == StagedDiff && m_diffPath == path)
        setDiffView(UnstagedDiff, path, {});
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::UnstagePaths;
        entry.paths = paths;
        entry.title = tr("Unstage %1").arg(path);
        enqueuePlanEntry(entry);
        return;
    }

    const QStringList command = m_status.headSha.isEmpty()
        ? QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-q")}
        : QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD")};
    enqueueCommand(tr("Unstaging %1").arg(path), command + QStringList{QStringLiteral("--")} + paths);
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
    if (m_diffSource != UnstagedDiff && m_diffSource != StagedDiff)
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

void Repository::commit(const QString &message, bool amend)
{
    if (m_plan) {
        PlanEntry entry;
        entry.kind = PlanEntry::Commit;
        entry.message = message;
        entry.amend = amend;
        const QString subject = message.section(QLatin1Char('\n'), 0, 0).trimmed();
        entry.title = amend ? tr("Amend the last commit as “%1”").arg(subject)
                            : tr("Commit “%1”").arg(subject);
        enqueuePlanEntry(entry);
        return;
    }

    QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend)
        args.append(QStringLiteral("--amend"));
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

QString Repository::foldBlocker(const QString &sha) const
{
    if (m_diffSource != UnstagedDiff && m_diffSource != StagedDiff)
        return tr("Show a working tree file and select the lines to fold.");
    if (m_diff.selectedCount() == 0)
        return tr("Select the lines to fold first.");
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
                                         QStringLiteral("igitt: undo fold"), QStringLiteral("HEAD"),
                                         fold.oldHead, fold.newHead});
        if (!moved.ok()) {
            outcome.error = tr("Undoing the fold failed: %1").arg(moved.errorText());
        } else if (!fold.stagedPatch.isEmpty()) {
            const GitResult unstaged = git.run({QStringLiteral("apply"), QStringLiteral("--cached"),
                                                QStringLiteral("--reverse"), QStringLiteral("--whitespace=nowarn")},
                                               fold.stagedPatch);
            if (!unstaged.ok())
                outcome.error = tr("The fold was undone, but unstaging the folded lines failed: %1")
                                    .arg(unstaged.errorText());
        }
        if (outcome.error.isEmpty())
            outcome.notice = tr("Fold undone.");
        outcome.state = loadState(sourceFor(git, nullptr), request, history);
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
        if (plan->add(entry, error))
            outcome.committed = entry.kind == PlanEntry::Commit;
        else
            outcome.error = tr("Could not plan “%1”: %2").arg(entry.title, error);
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

void Repository::setDiffView(DiffSource source, const QString &path, const QString &commit)
{
    m_diffSource = source;
    m_diffPath = path;
    m_diffCommit = commit;
    if (source != CommitDiff)
        m_commitDetails.clear();
    m_diffConflicted = false;
    ++*m_diffGeneration;
    emit diffChanged();
}

DiffRequest Repository::diffRequest() const
{
    return {diffKind(m_diffSource), m_diffPath, m_diffCommit, *m_diffGeneration};
}

HistoryRequest Repository::historyRequest(bool force) const
{
    return {force, m_historyHead, m_historyWip, m_filter, m_pushSettings};
}

QString Repository::pushMode() const
{
    switch (m_pushSettings.mode) {
    case PushMode::Plain: return QStringLiteral("plain");
    case PushMode::Gerrit: return QStringLiteral("gerrit");
    case PushMode::Auto: break;
    }
    return QStringLiteral("auto");
}

void Repository::setPushMode(const QString &mode)
{
    const PushMode value = mode == QLatin1StringView("plain")    ? PushMode::Plain
                         : mode == QLatin1StringView("gerrit") ? PushMode::Gerrit
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

void Repository::push(bool force)
{
    if (m_plan) {
        emit errorOccurred(tr("Pushing is off while planning; execute or discard the plan first."));
        return;
    }
    if (const QString op = operation(); !op.isEmpty()) {
        emit errorOccurred(tr("git is in the middle of an operation (%1); finish or abort it first.").arg(op));
        return;
    }
    enqueue(JobKind::Write, [force, settings = m_pushSettings, request = diffRequest(),
                             history = historyRequest(true)](const GitRunner &git) {
        Outcome outcome;
        // Resolved again: the branch or its configuration may have changed.
        const PushTarget target = resolvePushTarget(git, settings);
        const NetworkResult result = pushTo(git, target, force);
        if (result.ok) {
            outcome.notice = target.gerrit ? tr("Pushed for review to refs/for/%1.").arg(target.remoteBranch)
                                           : tr("Pushed to %1/%2.").arg(target.remote, target.remoteBranch);
            if (!result.links.isEmpty())
                outcome.notice += QLatin1Char(' ') + result.links.join(QStringLiteral(" "));
        } else if (result.rejected && !target.gerrit && !force) {
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

QByteArray Repository::selectionPatch(PatchDirection direction) const
{
    QByteArray patch;
    for (int f = 0; f < m_diff.files().size(); ++f)
        patch += buildPatch(m_diff.files()[f], m_diff.selection(f), direction);
    return patch;
}
