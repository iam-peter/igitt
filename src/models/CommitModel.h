#pragma once

#include "core/GraphLayout.h"
#include "core/LogParser.h"

#include <QAbstractListModel>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtQml/qqmlregistration.h>

class CommitModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by Repository")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)

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
        MatchedRole, // true unless a search is active and the row doesn't match
        ReviewRole,  // {state, number, patchSet, url} for commits pushed for review
    };

    using QAbstractListModel::QAbstractListModel;

    // `graph` is layoutGraph(commits), computed off the UI thread.
    // `planned` are commits that only exist in a plan's simulation.
    void setCommits(const QList<CommitInfo> &commits, const QList<GraphRow> &graph,
                    const QList<RefInfo> &refs, const QString &headSha, const QString &currentBranch,
                    const QSet<QString> &planned = {});
    // Stats never change for a commit, so they are kept across reloads.
    void setStats(const QHash<QString, CommitStats> &stats);
    // What Gerrit has for commits a push for review would send; by SHA.
    void setReviews(const QHash<QString, QVariantMap> &reviews);
    bool hasStats(const QString &sha) const { return m_stats.contains(sha); }

    int count() const { return int(m_commits.size()); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString shaAt(int row) const;
    Q_INVOKABLE QString subjectAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &sha) const;

    // Search terms separated by spaces must all match; quote a phrase to
    // keep it together. `author:`, `message:` and `sha:` restrict a term
    // to that field; a plain term matches any of them (SHAs by prefix).
    Q_INVOKABLE void setSearch(const QString &query, bool caseSensitive = false, bool regex = false);
    bool searching() const { return !m_terms.isEmpty(); }
    int matchCount() const { return m_matchCount; }
    QString searchError() const { return m_searchError; }
    Q_INVOKABLE bool matches(int row) const;
    // The next match after `from` (before it if not `forward`), wrapping
    // around; -1 if there is none.
    Q_INVOKABLE int findNext(int from, bool forward = true) const;

    // For painting the activity minimap.
    qint64 timestampAt(int row) const;
    bool hasRefsAt(int row) const;
    QString laneColorAt(int row) const;
    int headRow() const { return rowOf(m_headSha); }

signals:
    void countChanged();
    void searchChanged();

private:
    struct SearchTerm
    {
        enum Field { Any, Author, Message, Sha };
        Field field = Any;
        QString text;
        QRegularExpression pattern; // when searching with regular expressions
    };

    QVariantList refLabels(const QList<RefInfo> &refs, const QString &color) const;
    bool termMatches(const SearchTerm &term, const CommitInfo &commit) const;
    void updateMatches();

    QList<CommitInfo> m_commits;
    QList<GraphRow> m_graph;
    QHash<QString, QVariantList> m_refs;
    QHash<QString, int> m_rowBySha;
    QHash<QString, CommitStats> m_stats;
    QHash<QString, QVariantMap> m_reviews;
    QString m_headSha;
    QString m_currentBranch;
    QSet<QString> m_planned;

    QList<SearchTerm> m_terms;
    bool m_caseSensitive = false;
    bool m_regex = false;
    QString m_searchError;
    QList<bool> m_matched;
    int m_matchCount = 0;
};
