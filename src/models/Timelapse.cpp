#include "Timelapse.h"

#include <QVariantMap>

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
    loadLines();
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
        m_revision = -1;
        emit revisionsChanged();
        if (revisions.isEmpty()) {
            setError(tr("%1 has no committed history.").arg(m_path));
            beginResetModel();
            m_lines.clear();
            endResetModel();
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
    run<Lines>([git = GitRunner(m_workDir), revisions = m_revisions, index = m_revision, deleted = m_showDeleted] {
        Lines result;
        result.lines = loadTimelapse(git, revisions, index, deleted, &result.error);
        return result;
    }, [this, generation](const Lines &result) {
        if (generation != m_generation)
            return;
        beginResetModel();
        m_lines = result.lines;
        endResetModel();
        setError(result.error);
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
    case DeletedRole: return line.kind == TimelapseLine::Deleted;
    case TextRole: return line.text;
    case LineNoRole: return line.lineNo;
    case RevisionRole: return line.revision;
    case ShortShaRole: return line.sha.left(8);
    case AuthorRole: return line.author;
    case AgeRole:
        if (line.revision < 0 || m_revisions.size() < 2)
            return line.revision < 0 ? 0.0 : 1.0;
        return qreal(line.revision) / qreal(m_revisions.size() - 1);
    case ChangedHereRole: return line.changedHere;
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
        {ChangedHereRole, "changedHere"},
    };
}
