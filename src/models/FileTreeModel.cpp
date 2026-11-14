#include "FileTreeModel.h"

#include <QHash>

#include <functional>

#include <algorithm>

namespace {

// Directories first, then files, case-insensitively
bool before(const FileTreeNode &a, const FileTreeNode &b)
{
    if (a.isDir != b.isDir)
        return a.isDir;
    return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
}

void renumber(FileTreeNode *dir, size_t from)
{
    for (size_t i = from; i < dir->children.size(); ++i)
        dir->children[i]->row = int(i);
}

} // namespace

QString fileStatus(const StatusEntry &entry)
{
    if (entry.conflicted)
        return QStringLiteral("U");
    if (entry.untracked)
        return QStringLiteral("A");
    if (entry.index == QLatin1Char('A'))
        return entry.worktree == QLatin1Char('D') ? QString() : QStringLiteral("A");
    if (entry.index == QLatin1Char('D') || entry.worktree == QLatin1Char('D'))
        return QStringLiteral("D");
    if (entry.index == QLatin1Char('R') || entry.index == QLatin1Char('C'))
        return QString(entry.index);
    return QStringLiteral("M");
}

QString directoryStatus(const QSet<QString> &statuses)
{
    if (statuses.isEmpty())
        return {};
    if (statuses.contains(QStringLiteral("U")))
        return QStringLiteral("U");
    if (statuses.size() == 1 && (statuses.contains(QStringLiteral("A")) || statuses.contains(QStringLiteral("D"))))
        return *statuses.cbegin();
    return QStringLiteral("M");
}

std::shared_ptr<FileTreeNode> buildFileTree(const QStringList &paths)
{
    auto root = std::make_shared<FileTreeNode>();
    root->isDir = true;
    QHash<QString, FileTreeNode *> dirs{{QString(), root.get()}};

    for (const QString &path : paths) {
        const QStringList parts = path.split(QLatin1Char('/'));
        FileTreeNode *parent = root.get();
        QString prefix;
        for (qsizetype i = 0; i < parts.size(); ++i) {
            prefix += (i ? QStringLiteral("/") : QString()) + parts[i];
            const bool isDir = i + 1 < parts.size();
            if (isDir && dirs.contains(prefix)) {
                parent = dirs.value(prefix);
                continue;
            }
            auto child = std::make_unique<FileTreeNode>();
            child->name = parts[i];
            child->path = prefix;
            child->isDir = isDir;
            child->parent = parent;
            FileTreeNode *raw = child.get();
            parent->children.push_back(std::move(child));
            if (isDir)
                dirs.insert(prefix, raw);
            parent = raw;
        }
    }

    // Sort and number every level.
    std::vector<FileTreeNode *> pending{root.get()};
    while (!pending.empty()) {
        FileTreeNode *dir = pending.back();
        pending.pop_back();
        std::sort(dir->children.begin(), dir->children.end(),
                  [](const auto &a, const auto &b) { return before(*a, *b); });
        for (size_t i = 0; i < dir->children.size(); ++i) {
            dir->children[i]->row = int(i);
            if (dir->children[i]->isDir)
                pending.push_back(dir->children[i].get());
        }
    }
    return root;
}

void FileTreeModel::setFiles(const QStringList &paths, std::shared_ptr<FileTreeNode> root)
{
    // A reset would collapse every expanded directory in the view.
    if (paths == m_paths)
        return;
    beginResetModel();
    m_paths = paths;
    m_root = root ? std::move(root) : std::make_shared<FileTreeNode>();
    m_untracked.clear();
    endResetModel();
    emit filesChanged();
    applyStatus();
}

void FileTreeModel::setStatus(const QList<StatusEntry> &entries)
{
    m_entries = entries;
    applyStatus();
}

void FileTreeModel::applyStatus()
{
    // Each file's status; a renamed file's old name is deleted
    QHash<QString, QString> files;
    for (const StatusEntry &entry : std::as_const(m_entries)) {
        const QString status = fileStatus(entry);
        if (status.isEmpty())
            continue;
        files.insert(entry.path, status);
        if (status == QLatin1String("R") && !entry.origPath.isEmpty())
            files.insert(entry.origPath, QStringLiteral("D"));
    }

    // Files not tracked come in while they are changes, and go after
    const QSet<QString> tracked(m_paths.cbegin(), m_paths.cend());
    for (const QString &path : QSet<QString>(m_untracked)) {
        if (!files.contains(path)) {
            if (FileTreeNode *n = find(path))
                remove(n);
            m_untracked.remove(path);
        }
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!tracked.contains(it.key()) && !m_untracked.contains(it.key())) {
            insert(it.key(), false);
            m_untracked.insert(it.key());
        }
    }

    // Every node's status, the directories' from what is in them
    const std::function<QString(FileTreeNode *)> mark = [&](FileTreeNode *n) {
        QString status;
        if (n->isDir) {
            QSet<QString> inside;
            for (const auto &child : n->children) {
                const QString childStatus = mark(child.get());
                if (!childStatus.isEmpty())
                    inside.insert(childStatus);
            }
            status = directoryStatus(inside);
        } else {
            status = files.value(n->path);
        }
        if (n != m_root.get() && n->status != status) {
            n->status = status;
            const QModelIndex index = indexOf(n);
            emit dataChanged(index, index, {StatusRole});
        }
        return status;
    };
    mark(m_root.get());

    QVariantMap statuses;
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        statuses.insert(it.key(), it.value());
    if (statuses != m_statuses) {
        m_statuses = statuses;
        emit statusChanged();
    }
}

FileTreeNode *FileTreeModel::find(const QString &path) const
{
    FileTreeNode *n = m_root.get();
    for (const QString &part : path.split(QLatin1Char('/'))) {
        const auto it = std::find_if(n->children.cbegin(), n->children.cend(),
                                     [&part](const auto &child) { return child->name == part; });
        if (it == n->children.cend())
            return nullptr;
        n = it->get();
    }
    return n;
}

FileTreeNode *FileTreeModel::insert(const QString &path, bool isDir)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    FileTreeNode *dir = m_root.get();
    if (slash >= 0) {
        const QString dirPath = path.left(slash);
        dir = find(dirPath);
        if (!dir)
            dir = insert(dirPath, true);
    }
    auto child = std::make_unique<FileTreeNode>();
    child->name = path.mid(slash + 1);
    child->path = path;
    child->isDir = isDir;
    child->parent = dir;
    const auto at = std::lower_bound(dir->children.begin(), dir->children.end(), child,
                                     [](const auto &a, const auto &b) { return before(*a, *b); });
    const int row = int(at - dir->children.begin());
    beginInsertRows(indexOf(dir), row, row);
    FileTreeNode *raw = child.get();
    dir->children.insert(at, std::move(child));
    renumber(dir, row);
    endInsertRows();
    return raw;
}

void FileTreeModel::remove(FileTreeNode *node)
{
    FileTreeNode *dir = node->parent;
    const int row = node->row;
    beginRemoveRows(indexOf(dir), row, row);
    dir->children.erase(dir->children.begin() + row);
    renumber(dir, row);
    endRemoveRows();
    // A directory that held only files not tracked goes with them
    if (dir != m_root.get() && dir->children.empty())
        remove(dir);
}

QStringList FileTreeModel::match(const QString &filter, int limit) const
{
    const QStringList terms = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList found;
    QStringList paths = m_paths;
    for (const QString &path : m_untracked)
        paths.append(path);
    for (const QString &path : std::as_const(paths)) {
        const bool all = std::all_of(terms.cbegin(), terms.cend(), [&path](const QString &term) {
            return path.contains(term, Qt::CaseInsensitive);
        });
        if (all && found.size() < limit)
            found.append(path);
    }
    return found;
}

const FileTreeNode *FileTreeModel::node(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<const FileTreeNode *>(index.internalPointer()) : m_root.get();
}

QModelIndex FileTreeModel::indexOf(const FileTreeNode *n) const
{
    if (!n || n == m_root.get())
        return {};
    return createIndex(n->row, 0, const_cast<FileTreeNode *>(n));
}

QModelIndex FileTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    const FileTreeNode *dir = node(parent);
    if (column != 0 || row < 0 || row >= int(dir->children.size()))
        return {};
    return createIndex(row, 0, dir->children[row].get());
}

QModelIndex FileTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    const FileTreeNode *parent = node(child)->parent;
    if (!parent || parent == m_root.get())
        return {};
    return createIndex(parent->row, 0, const_cast<FileTreeNode *>(parent));
}

int FileTreeModel::rowCount(const QModelIndex &parent) const
{
    return parent.column() > 0 ? 0 : int(node(parent)->children.size());
}

int FileTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant FileTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const FileTreeNode *n = node(index);
    switch (role) {
    case Qt::DisplayRole: return n->name;
    case PathRole: return n->path;
    case IsDirRole: return n->isDir;
    case StatusRole: return n->status;
    }
    return {};
}

QHash<int, QByteArray> FileTreeModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "fileName"},
        {PathRole, "path"},
        {IsDirRole, "isDir"},
        {StatusRole, "status"},
    };
}
