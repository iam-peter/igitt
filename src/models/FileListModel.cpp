#include "FileListModel.h"

FileListModel::FileListModel(bool staged, QObject *parent)
    : QAbstractListModel(parent)
    , m_staged(staged)
{
}

void FileListModel::setEntries(const QList<StatusEntry> &entries)
{
    const int oldCount = count();
    beginResetModel();
    m_entries.clear();
    for (const StatusEntry &e : entries) {
        if (m_staged ? e.hasStagedChange() : e.hasUnstagedChange())
            m_entries.append(e);
    }
    endResetModel();
    if (count() != oldCount)
        emit countChanged();
}

const StatusEntry *FileListModel::entry(const QString &path) const
{
    for (const StatusEntry &e : m_entries) {
        if (e.path == path)
            return &e;
    }
    return nullptr;
}

int FileListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant FileListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const StatusEntry &e = m_entries[index.row()];
    switch (role) {
    case PathRole: return e.path;
    case OrigPathRole: return e.origPath;
    case StatusRole:
        if (e.conflicted)
            return QStringLiteral("U");
        return QString(m_staged ? e.index : e.worktree);
    case UntrackedRole: return e.untracked;
    case ConflictedRole: return e.conflicted;
    }
    return {};
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    return {
        {PathRole, "path"},
        {OrigPathRole, "origPath"},
        {StatusRole, "status"},
        {UntrackedRole, "untracked"},
        {ConflictedRole, "conflicted"},
    };
}
