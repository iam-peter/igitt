#include "FileTreeModel.h"

#include <QHash>

#include <algorithm>

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
        std::sort(dir->children.begin(), dir->children.end(), [](const auto &a, const auto &b) {
            if (a->isDir != b->isDir)
                return a->isDir;
            return QString::compare(a->name, b->name, Qt::CaseInsensitive) < 0;
        });
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
    endResetModel();
    emit filesChanged();
}

QStringList FileTreeModel::match(const QString &filter, int limit) const
{
    const QStringList terms = filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList found;
    for (const QString &path : m_paths) {
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
    }
    return {};
}

QHash<int, QByteArray> FileTreeModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "fileName"},
        {PathRole, "path"},
        {IsDirRole, "isDir"},
    };
}
