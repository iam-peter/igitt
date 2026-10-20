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
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        ShaRole = Qt::UserRole + 1,
        ShortShaRole,
        SubjectRole,
        AuthorRole,
        AuthorEmailRole,
        InitialsRole,
        DateRole,
        RefsRole,
        GraphRole,
        LaneColorRole,
        IsHeadRole,
        IsMergeRole,
        PlannedRole,
        StatsRole,
    };

    using QAbstractListModel::QAbstractListModel;

    // `graph` is layoutGraph(commits), computed off the UI thread.
    // `planned` are commits that only exist in a plan's simulation.
    void setCommits(const QList<CommitInfo> &commits, const QList<GraphRow> &graph,
                    const QList<RefInfo> &refs, const QString &headSha, const QString &currentBranch,
                    const QSet<QString> &planned = {});
    // Stats never change for a commit, so they are kept across reloads.
    void setStats(const QHash<QString, CommitStats> &stats);
    bool hasStats(const QString &sha) const { return m_stats.contains(sha); }

    int count() const { return int(m_commits.size()); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString shaAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &sha) const;

    // Search: case-insensitive in subject and author, or a SHA prefix.
    Q_INVOKABLE bool matches(int row, const QString &query) const;
    Q_INVOKABLE int countMatches(const QString &query) const;
    // The next match after `from` (before it if not `forward`), wrapping
    // around; -1 if there is none.
    Q_INVOKABLE int findNext(const QString &query, int from, bool forward = true) const;

signals:
    void countChanged();

private:
    QVariantList refLabels(const QList<RefInfo> &refs, const QString &color) const;

    QList<CommitInfo> m_commits;
    QList<GraphRow> m_graph;
    QHash<QString, QVariantList> m_refs;
    QHash<QString, int> m_rowBySha;
    QHash<QString, CommitStats> m_stats;
    QString m_headSha;
    QString m_currentBranch;
    QSet<QString> m_planned;
};
