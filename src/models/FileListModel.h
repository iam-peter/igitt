#pragma once

#include "core/StatusParser.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// Files of one side of the status: either the unstaged or the staged ones.
class FileListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Repository")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        OrigPathRole,
        StatusRole,
        UntrackedRole,
        ConflictedRole,
    };

    FileListModel(bool staged, QObject *parent = nullptr);

    void setEntries(const QList<StatusEntry> &entries);
    const StatusEntry *entry(const QString &path) const;
    int count() const { return int(m_entries.size()); }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    bool m_staged;
    QList<StatusEntry> m_entries;
};
