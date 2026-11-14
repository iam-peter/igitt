#pragma once

#include "core/StatusParser.h"

#include <QAbstractItemModel>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

// A directory tree, built off the UI thread from a list of paths.
struct FileTreeNode
{
    QString name;
    QString path; // relative to the repository; directories too
    bool isDir = false;
    // Against HEAD, as fileStatus() gives it; a directory's sums up the
    // files in it
    QString status;
    FileTreeNode *parent = nullptr;
    int row = 0; // among the parent's children
    std::vector<std::unique_ptr<FileTreeNode>> children;
};

// Directories first, then files, each alphabetically.
std::shared_ptr<FileTreeNode> buildFileTree(const QStringList &paths);

// What the working tree and the index together did to a file against
// HEAD, as a letter of git status: A added (untracked too), M modified,
// D deleted, R renamed, C copied, U unmerged; empty for nothing, as for a
// file added and deleted again.
QString fileStatus(const StatusEntry &entry);

// A directory's status from those of the files in it: the one they share
// if they are all added or all deleted, U if any is unmerged, otherwise M.
QString directoryStatus(const QSet<QString> &statuses);

// The repository's tracked files, for the Files tab, marked with their
// changes against HEAD; files not tracked yet, and those deleted from the
// index, are in it too while they are changes.
class FileTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Repository")
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged)
    // {path: status} of the files changed, for lists of paths
    Q_PROPERTY(QVariantMap statuses READ statuses NOTIFY statusChanged)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        IsDirRole,
        StatusRole,
    };

    using QAbstractItemModel::QAbstractItemModel;

    void setFiles(const QStringList &paths, std::shared_ptr<FileTreeNode> root);
    int fileCount() const { return int(m_paths.size()); }
    // From `git status`; keeps the tree as it is, expanded directories
    // too, adding and removing only the rows of files not tracked.
    void setStatus(const QList<StatusEntry> &entries);
    QVariantMap statuses() const { return m_statuses; }

    // Paths containing every space-separated term, case-insensitively, at
    // most `limit` of them; for filtering the tree.
    Q_INVOKABLE QStringList match(const QString &filter, int limit = 500) const;

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void filesChanged();
    void statusChanged();

private:
    const FileTreeNode *node(const QModelIndex &index) const;
    QModelIndex indexOf(const FileTreeNode *node) const;
    FileTreeNode *find(const QString &path) const;
    FileTreeNode *insert(const QString &path, bool isDir);
    void remove(FileTreeNode *node);
    void applyStatus();

    QStringList m_paths;
    QList<StatusEntry> m_entries;
    QVariantMap m_statuses;
    QSet<QString> m_untracked; // files in the tree only as changes
    std::shared_ptr<FileTreeNode> m_root = std::make_shared<FileTreeNode>();
};
