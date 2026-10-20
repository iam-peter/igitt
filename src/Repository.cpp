#include "Repository.h"

#include "core/FoldEngine.h"
#include "core/PatchBuilder.h"

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

    m_pool.start([this, kind, git = m_git, job = std::move(job)] {
        Outcome outcome = job(git);
        if (outcome.workDir.isEmpty())
            outcome.workDir = git.workDir();
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

    if (!outcome.openedPath.isEmpty()) {
        m_git.setWorkDir(outcome.openedPath);
        m_historyHead.clear();
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

    // Results of jobs for a repository that has been replaced meanwhile
    // are of no use.
    if (outcome.workDir == m_git.workDir()) {
        if (outcome.fold) {
            m_lastFold = *outcome.fold;
            emit canUndoFoldChanged();
        }
        applyState(outcome.state);
        if (outcome.committed)
            emit committed();
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
        m_headMessage = history.headMessage;
        m_foldTargets = QSet<QString>(history.foldTargets.cbegin(), history.foldTargets.cend());
        m_unpublished = history.unpublished;
        m_commits.setCommits(history.commits, history.graph, history.refs, history.head, m_status.branch);
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

void Repository::open(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    enqueue(JobKind::Read, [path](const GitRunner &) {
        Outcome outcome;
        const GitResult result = GitRunner(path).run({QStringLiteral("rev-parse"),
                                                      QStringLiteral("--show-toplevel")});
        if (!result.ok()) {
            outcome.error = tr("%1 is not inside a git work tree: %2").arg(path, result.errorText());
            return outcome;
        }
        outcome.openedPath = QString::fromUtf8(result.out).trimmed();
        outcome.workDir = outcome.openedPath;
        outcome.state = loadState(GitRunner(outcome.openedPath), {}, {}, true);
        return outcome;
    });
}

void Repository::refresh()
{
    if (!valid())
        return;
    enqueue(JobKind::Read, [request = diffRequest()](const GitRunner &git) {
        Outcome outcome;
        outcome.state = loadState(git, request, {}, true);
        return outcome;
    });
}

void Repository::refreshStatus()
{
    if (!valid())
        return;
    enqueue(JobKind::Read, [request = diffRequest(), head = m_historyHead](const GitRunner &git) {
        Outcome outcome;
        outcome.state = loadState(git, request, head, false);
        return outcome;
    });
}

void Repository::showCommit(const QString &sha)
{
    if (sha.isEmpty() || !valid())
        return;
    setDiffView(CommitDiff, {}, sha);
    enqueue(JobKind::Read, [request = diffRequest(), current = m_diffGeneration](const GitRunner &git) {
        Outcome outcome;
        // Arrow keys through the history queue a load per row; only the
        // last one matters.
        if (request.generation == *current)
            outcome.state.diff = loadDiff(git, request, {});
        return outcome;
    });
}

void Repository::showFile(const QString &path, bool staged)
{
    if (!valid())
        return;
    setDiffView(staged ? StagedDiff : UnstagedDiff, path, {});
    enqueue(JobKind::Read, [request = diffRequest(), head = m_historyHead,
                            current = m_diffGeneration](const GitRunner &git) {
        Outcome outcome;
        if (request.generation == *current)
            outcome.state = loadState(git, request, head, false);
        return outcome;
    });
}

void Repository::stageFile(const QString &path)
{
    // Follow the file to the side it moves to.
    if (m_diffSource == UnstagedDiff && m_diffPath == path)
        setDiffView(StagedDiff, path, {});
    enqueueCommand(tr("Staging %1").arg(path), {QStringLiteral("add"), QStringLiteral("--"), path});
}

void Repository::unstageFile(const QString &path)
{
    QStringList paths{path};
    if (const StatusEntry *entry = m_staged.entry(path); entry && !entry->origPath.isEmpty())
        paths.append(entry->origPath);
    if (m_diffSource == StagedDiff && m_diffPath == path)
        setDiffView(UnstagedDiff, path, {});

    const QStringList command = m_status.headSha.isEmpty()
        ? QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-q")}
        : QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD")};
    enqueueCommand(tr("Unstaging %1").arg(path), command + QStringList{QStringLiteral("--")} + paths);
}

void Repository::stageAll()
{
    enqueueCommand(tr("Staging all changes"), {QStringLiteral("add"), QStringLiteral("-A")});
}

void Repository::unstageAll()
{
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

void Repository::commit(const QString &message, bool amend)
{
    QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend)
        args.append(QStringLiteral("--amend"));
    enqueue(JobKind::Write, [args, input = message.toUtf8(), request = diffRequest()](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run(args, input);
        outcome.committed = result.ok();
        if (!result.ok())
            outcome.error = tr("Committing failed: %1").arg(result.errorText());
        outcome.state = loadState(git, request, {}, true);
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
        return tr("Only commits of the current branch, from HEAD down to its last merge, can be folded into.");
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

    enqueue(JobKind::Write, [=, request = diffRequest()](const GitRunner &git) {
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
        outcome.state = loadState(git, request, {}, true);
        return outcome;
    });
}

void Repository::undoFold()
{
    if (!canUndoFold())
        return;
    const LastFold fold = m_lastFold;
    forgetFold();

    enqueue(JobKind::Write, [fold, request = diffRequest()](const GitRunner &git) {
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
        outcome.state = loadState(git, request, {}, true);
        return outcome;
    });
}

void Repository::enqueueCommand(const QString &what, const QStringList &args, const QByteArray &input)
{
    enqueue(JobKind::Write, [=, request = diffRequest(), head = m_historyHead](const GitRunner &git) {
        Outcome outcome;
        const GitResult result = git.run(args, input);
        if (!result.ok())
            outcome.error = tr("%1 failed: %2").arg(what, result.errorText());
        outcome.state = loadState(git, request, head, false);
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
    ++*m_diffGeneration;
    emit diffChanged();
}

DiffRequest Repository::diffRequest() const
{
    return {diffKind(m_diffSource), m_diffPath, m_diffCommit, *m_diffGeneration};
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
