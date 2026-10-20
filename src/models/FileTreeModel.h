#pragma once

#include <QAbstractItemModel>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

// A directory tree, built off the UI thread from a list of paths.
struct FileTreeNode
{
    QString name;
    QString path; // relative to the repository; directories too
    bool isDir = false;
    FileTreeNode *parent = nullptr;
    int row = 0; // among the parent's children
    std::vector<std::unique_ptr<FileTreeNode>> children;
};

// Directories first, then files, each alphabetically.
std::shared_ptr<FileTreeNode> buildFileTree(const QStringList &paths);

// The repository's tracked files, for the Files tab.
class FileTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Repository")
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        IsDirRole,
    };

    using QAbstractItemModel::QAbstractItemModel;

    void setFiles(const QStringList &paths, std::shared_ptr<FileTreeNode> root);
    int fileCount() const { return int(m_paths.size()); }

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

private:
    const FileTreeNode *node(const QModelIndex &index) const;

    QStringList m_paths;
    std::shared_ptr<FileTreeNode> m_root = std::make_shared<FileTreeNode>();
};
