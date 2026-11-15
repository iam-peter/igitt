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
    // Whether the history is narrowed to some commits (setFocus())
    Q_PROPERTY(bool focused READ focused NOTIFY searchChanged)
    // While git looks for the commits a `code:` term asks for
    Q_PROPERTY(bool searchingCode READ searchingCode NOTIFY searchChanged)

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
        ReviewRole,  // {kind, state, number, url, badges, details, ...}: Gerrit changes, or a branch's
                     // pull or merge request on its tip; see Repository::setReviews()
    };

    explicit CommitModel(QObject *parent = nullptr);

    // `graph` is layoutGraph(commits), computed off the UI thread.
    // `planned` are commits that only exist in a plan's simulation.
    void setCommits(const QList<CommitInfo> &commits, const QList<GraphRow> &graph,
                    const QList<RefInfo> &refs, const QString &headSha, const QString &currentBranch,
                    const QSet<QString> &planned = {});
    // Stats never change for a commit, so they are kept across reloads.
    void setStats(const QHash<QString, CommitStats> &stats);
    // What the review host has for commits a push for review would send,
    // or for the branch on its tip; by SHA.
    void setReviews(const QHash<QString, QVariantMap> &reviews);
    bool hasStats(const QString &sha) const { return m_stats.contains(sha); }

    int count() const { return int(m_commits.size()); }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString shaAt(int row) const;
    Q_INVOKABLE QString subjectAt(int row) const;
    // A row's roles by name, for a view of a few rows that are not the
    // model's own; empty past the end
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &sha) const;

    // Search terms separated by spaces must all match; quote a phrase to
    // keep it together. `author:`, `message:` and `sha:` restrict a term
    // to that field; a plain term matches any of them (SHAs by prefix).
    // `code:` matches the commits that added or removed the text, as
    // git log -S (-G with regular expressions) finds them: asked with
    // codeSearchRequested(), answered with setCodeMatches().
    Q_INVOKABLE void setSearch(const QString &query, bool caseSensitive = false, bool regex = false);
    bool searching() const { return !m_terms.isEmpty() || !m_focus.isEmpty(); }
    // Only these commits match, together with the search: the history of
    // a file, say. None for all of them.
    Q_INVOKABLE void setFocus(const QStringList &shas);
    bool focused() const { return !m_focus.isEmpty(); }
    int matchCount() const { return m_matchCount; }
    bool searchingCode() const { return !m_codePending.isEmpty(); }
    // The commits git found for a `code:` term searched as given
    void setCodeMatches(const QString &text, bool caseSensitive, bool regex, const QStringList &shas);
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
    // `code:` terms whose commits are not known yet
    void codeSearchRequested(const QStringList &texts, bool caseSensitive, bool regex);

private:
    struct SearchTerm
    {
        enum Field { Any, Author, Message, Sha, Code };
        Field field = Any;
        QString text;
        QRegularExpression pattern; // when searching with regular expressions
    };

    QVariantList refLabels(const QList<RefInfo> &refs) const;
    bool termMatches(const SearchTerm &term, const CommitInfo &commit) const;
    void updateMatches();
    QString codeKey(const QString &text) const;
    // Asks for the `code:` terms' commits that are not known yet
    void requestCode();

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
    QSet<QString> m_focus;
    // Commits found for `code:` terms, by codeKey(); for the history shown
    QHash<QString, QSet<QString>> m_codeMatches;
    QSet<QString> m_codePending;
};
