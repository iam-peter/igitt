#include "Timelapse.h"

#include <QVariantMap>

#include <algorithm>

Timelapse::Timelapse(QObject *parent)
    : QAbstractListModel(parent)
{
    m_pool.setMaxThreadCount(1);
}

Timelapse::~Timelapse()
{
    m_pool.clear();
    m_pool.waitForDone();
}

template <typename Result>
void Timelapse::run(std::function<Result()> job, std::function<void(Result)> done)
{
    if (m_pending++ == 0)
        emit busyChanged();
    m_pool.start([this, job, done] {
        Result result = job();
        QMetaObject::invokeMethod(this, [this, done, result = std::move(result)] {
            done(result);
            if (--m_pending == 0)
                emit busyChanged();
        }, Qt::QueuedConnection);
    });
}

void Timelapse::setWorkDir(const QString &dir)
{
    if (m_workDir == dir)
        return;
    m_workDir = dir;
    emit sourceChanged();
    loadRevisions();
}

void Timelapse::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    emit sourceChanged();
    loadRevisions();
}

void Timelapse::setRevision(int revision)
{
    revision = std::clamp(revision, 0, std::max(0, revisionCount() - 1));
    if (m_revision == revision || m_revisions.isEmpty())
        return;
    m_revision = revision;
    emit revisionChanged();
    loadLines();
}

void Timelapse::setShowDeleted(bool show)
{
    if (m_showDeleted == show)
        return;
    m_showDeleted = show;
    emit showDeletedChanged();
    showLines();
}

void Timelapse::setError(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        emit errorChanged();
    }
}

void Timelapse::loadRevisions()
{
    if (m_workDir.isEmpty() || m_path.isEmpty())
        return;
    const int generation = ++m_generation;
    run<QList<FileRevision>>([git = GitRunner(m_workDir), path = m_path] { return loadFileRevisions(git, path); },
                             [this, generation](const QList<FileRevision> &revisions) {
        if (generation != m_generation)
            return;
        m_revisions = revisions;
        m_flow.clear();
        ++m_flowGeneration;
        emit flowChanged();
        if (m_flowWanted)
            loadFlow();
        m_revision = -1;
        emit revisionsChanged();
        if (revisions.isEmpty()) {
            setError(tr("%1 has no committed history.").arg(m_path));
            m_allLines.clear();
            showLines();
            return;
        }
        setError({});
        setRevision(int(revisions.size()) - 1);
    });
}

void Timelapse::loadLines()
{
    if (m_revision < 0 || m_revision >= m_revisions.size())
        return;
    const int generation = ++m_generation;
    struct Lines
    {
        QList<TimelapseLine> lines;
        QString error;
    };
    // With the deleted lines always, which showLines() leaves out when
    // hidden, still marking where they were
    run<Lines>([git = GitRunner(m_workDir), revisions = m_revisions, index = m_revision] {
        Lines result;
        result.lines = loadTimelapse(git, revisions, index, true, &result.error);
        return result;
    }, [this, generation](const Lines &result) {
        if (generation != m_generation)
            return;
        m_allLines = result.lines;
        showLines();
        setError(result.error);
    });
}

void Timelapse::showLines()
{
    beginResetModel();
    m_lines.clear();
    m_changes.clear();
    m_hiddenRemovals.clear();
    m_lineOfRow.clear();
    int line = 0;
    for (const TimelapseLine &l : std::as_const(m_allLines)) {
        const bool removed = l.change == LineChange::Removed;
        if (removed && !m_showDeleted) {
            const int row = int(m_lines.size());
            if (m_hiddenRemovals.isEmpty() || m_hiddenRemovals.last() != row)
                m_hiddenRemovals.append(row);
            continue;
        }
        m_lines.append(l);
        m_changes.append(int(l.change));
        m_lineOfRow.append(line);
        if (!removed)
            ++line;
    }
    endResetModel();
    emit changesChanged();
}

qreal Timelapse::lineAt(qreal row) const
{
    if (m_lines.isEmpty() || row <= 0)
        return 0;
    const int whole = int(row);
    if (whole >= m_lines.size())
        return m_lineOfRow.last() + (m_lines.last().change == LineChange::Removed ? 0 : 1);
    const bool removed = m_lines[whole].change == LineChange::Removed;
    return m_lineOfRow[whole] + (removed ? 0 : row - whole);
}

qreal Timelapse::rowOf(qreal line) const
{
    if (m_lines.isEmpty())
        return 0;
    line = std::max(0.0, line);
    const int whole = int(line);
    // The first row of that line that is not a deleted one
    const auto it = std::lower_bound(m_lineOfRow.cbegin(), m_lineOfRow.cend(), whole);
    int row = int(it - m_lineOfRow.cbegin());
    while (row < m_lines.size() && m_lines[row].change == LineChange::Removed)
        ++row;
    if (row >= m_lines.size())
        return m_lines.size();
    return row + (line - whole);
}

void Timelapse::loadFlow()
{
    m_flowWanted = true;
    if (!m_flow.isEmpty() || m_revisions.isEmpty())
        return;
    const int generation = ++m_flowGeneration;
    run<QList<FlowStep>>([git = GitRunner(m_workDir), revisions = m_revisions] { return loadFileFlow(git, revisions); },
                         [this, generation](const QList<FlowStep> &steps) {
        if (generation != m_flowGeneration)
            return;
        m_flow.clear();
        for (const FlowStep &step : steps) {
            QVariantList runs;
            for (const FlowRun &run : step.runs) {
                runs.append(QVariantMap{{QStringLiteral("kind"), int(run.kind)},
                                        {QStringLiteral("oldStart"), run.oldStart},
                                        {QStringLiteral("oldCount"), run.oldCount},
                                        {QStringLiteral("newStart"), run.newStart},
                                        {QStringLiteral("newCount"), run.newCount}});
            }
            m_flow.append(QVariantMap{{QStringLiteral("lines"), step.lines},
                                      {QStringLiteral("added"), step.added},
                                      {QStringLiteral("changed"), step.changed},
                                      {QStringLiteral("removed"), step.removed},
                                      {QStringLiteral("binary"), step.binary},
                                      {QStringLiteral("runs"), runs}});
        }
        emit flowChanged();
    });
}

QVariantList Timelapse::revisions() const
{
    QVariantList list;
    for (const FileRevision &r : m_revisions) {
        list.append(QVariantMap{
            {QStringLiteral("sha"), r.sha},
            {QStringLiteral("shortSha"), r.sha.left(10)},
            {QStringLiteral("author"), r.author},
            {QStringLiteral("date"), r.date.toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
            {QStringLiteral("time"), r.date.toSecsSinceEpoch()},
            {QStringLiteral("subject"), r.subject},
            {QStringLiteral("path"), r.path},
        });
    }
    return list;
}

int Timelapse::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_lines.size());
}

QVariant Timelapse::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const TimelapseLine &line = m_lines[index.row()];
    switch (role) {
    case DeletedRole: return line.change == LineChange::Removed;
    case TextRole: return line.text;
    case LineNoRole: return line.lineNo;
    case RevisionRole: return line.revision;
    case ShortShaRole: return line.sha.left(8);
    case AuthorRole: return line.author;
    case AgeRole:
        if (line.revision < 0 || m_revisions.size() < 2)
            return line.revision < 0 ? 0.0 : 1.0;
        return qreal(line.revision) / qreal(m_revisions.size() - 1);
    case ChangeRole: return int(line.change);
    }
    return {};
}

QHash<int, QByteArray> Timelapse::roleNames() const
{
    return {
        {DeletedRole, "deleted"},
        {TextRole, "text"},
        {LineNoRole, "lineNo"},
        {RevisionRole, "revision"},
        {ShortShaRole, "shortSha"},
        {AuthorRole, "author"},
        {AgeRole, "age"},
        {ChangeRole, "change"},
    };
}
