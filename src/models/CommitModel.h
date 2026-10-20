#pragma once

#include "core/GraphLayout.h"
#include "core/LogParser.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QtQml/qqmlregistration.h>

class CommitModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Repository")

public:
    enum Roles {
        ShaRole = Qt::UserRole + 1,
        ShortShaRole,
        SubjectRole,
        AuthorRole,
        AuthorEmailRole,
        DateRole,
        RefsRole,
        GraphRole,
        IsHeadRole,
        PlannedRole,
    };

    using QAbstractListModel::QAbstractListModel;

    // `graph` is layoutGraph(commits), computed off the UI thread.
    // `planned` are commits that only exist in a plan's simulation.
    void setCommits(const QList<CommitInfo> &commits, const QList<GraphRow> &graph,
                    const QList<RefInfo> &refs, const QString &headSha, const QString &currentBranch,
                    const QSet<QString> &planned = {});

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString shaAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &sha) const;

private:
    QList<CommitInfo> m_commits;
    QList<GraphRow> m_graph;
    QHash<QString, QVariantList> m_refs;
    QHash<QString, int> m_rowBySha;
    QString m_headSha;
    QSet<QString> m_planned;
};
