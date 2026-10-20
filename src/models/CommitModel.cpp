#include "CommitModel.h"

#include <QVariantMap>

namespace {

QString initials(const QString &name)
{
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return QStringLiteral("?");
    if (words.size() == 1)
        return words.first().left(2).toUpper();
    return (words.first().left(1) + words.last().left(1)).toUpper();
}

QVariantMap label(const QString &name, const QString &kind, const QString &color, bool current = false,
                  const QStringList &remotes = {})
{
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("color"), color},
        {QStringLiteral("current"), current},
        {QStringLiteral("remotes"), remotes},
    };
}

} // namespace

void CommitModel::setCommits(const QList<CommitInfo> &commits, const QList<GraphRow> &graph,
                             const QList<RefInfo> &refs, const QString &headSha,
                             const QString &currentBranch, const QSet<QString> &planned)
{
    Q_ASSERT(graph.size() == commits.size());
    const int oldCount = count();
    beginResetModel();
    m_commits = commits;
    m_graph = graph;
    m_headSha = headSha;
    m_currentBranch = currentBranch;
    m_planned = planned;

    m_rowBySha.clear();
    m_rowBySha.reserve(commits.size());
    for (int i = 0; i < commits.size(); ++i)
        m_rowBySha.insert(commits[i].sha, i);

    QHash<QString, QList<RefInfo>> refsBySha;
    for (const RefInfo &ref : refs)
        refsBySha[ref.sha].append(ref);
    const bool detached = !headSha.isEmpty() && currentBranch.isEmpty();
    if (detached)
        refsBySha.try_emplace(headSha);
    m_refs.clear();
    for (auto it = refsBySha.cbegin(); it != refsBySha.cend(); ++it) {
        const int row = m_rowBySha.value(it.key(), -1);
        if (row < 0)
            continue;
        const QString color = laneColorName(m_graph[row].color);
        QVariantList labels = refLabels(it.value(), color);
        if (detached && it.key() == headSha)
            labels.prepend(label(QStringLiteral("HEAD"), QStringLiteral("head"), color, true));
        m_refs.insert(it.key(), labels);
    }
    updateMatches();
    endResetModel();
    if (count() != oldCount)
        emit countChanged();
    if (searching())
        emit searchChanged();
}

// One label per local branch, carrying the remote-tracking branches of
// the same name that point at the same commit, as GitKraken shows them.
QVariantList CommitModel::refLabels(const QList<RefInfo> &refs, const QString &color) const
{
    QVariantList current;
    QVariantList locals;
    QVariantList others;
    QHash<QString, QStringList> remotesByName;
    QSet<QString> localNames;
    for (const RefInfo &ref : refs) {
        if (ref.kind == RefInfo::LocalBranch)
            localNames.insert(ref.name);
    }
    for (const RefInfo &ref : refs) {
        if (ref.kind == RefInfo::RemoteBranch) {
            const QString shortName = ref.name.section(QLatin1Char('/'), 1);
            if (localNames.contains(shortName))
                remotesByName[shortName].append(ref.name);
            else
                others.append(label(ref.name, QStringLiteral("remote"), color));
        }
    }
    for (const RefInfo &ref : refs) {
        switch (ref.kind) {
        case RefInfo::LocalBranch: {
            const bool isCurrent = ref.name == m_currentBranch;
            (isCurrent ? current : locals)
                .append(label(ref.name, QStringLiteral("branch"), color, isCurrent, remotesByName.value(ref.name)));
            break;
        }
        case RefInfo::Tag:
            others.append(label(ref.name, QStringLiteral("tag"), color));
            break;
        case RefInfo::Stash:
            others.append(label(ref.name, QStringLiteral("stash"), color));
            break;
        case RefInfo::RemoteBranch:
            break;
        }
    }
    return current + locals + others;
}

void CommitModel::setReviews(const QHash<QString, QVariantMap> &reviews)
{
    m_reviews = reviews;
    if (count() > 0)
        emit dataChanged(index(0), index(count() - 1), {ReviewRole});
}

void CommitModel::setStats(const QHash<QString, CommitStats> &stats)
{
    for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
        m_stats.insert(it.key(), it.value());
        const int row = m_rowBySha.value(it.key(), -1);
        if (row >= 0)
            emit dataChanged(index(row), index(row), {StatsRole});
    }
}

int CommitModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant CommitModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const CommitInfo &c = m_commits[index.row()];
    switch (role) {
    case ShaRole: return c.sha;
    case ShortShaRole: return c.sha.left(10);
    case SubjectRole: return c.subject;
    case AuthorRole: return c.authorName;
    case AuthorEmailRole: return c.authorEmail;
    case InitialsRole: return initials(c.authorName);
    case DateRole: return c.authorDate.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    case RefsRole: return m_refs.value(c.sha);
    case GraphRole: return QVariant::fromValue(m_graph[index.row()]);
    case LaneColorRole: return laneColorName(m_graph[index.row()].color);
    case IsHeadRole: return c.sha == m_headSha;
    case IsMergeRole: return c.parents.size() > 1;
    case PlannedRole: return m_planned.contains(c.sha);
    case MatchedRole: return matches(index.row()) || !searching();
    case ReviewRole: {
        const auto it = m_reviews.constFind(c.sha);
        return it == m_reviews.cend() ? QVariant() : QVariant(*it);
    }
    case StatsRole: {
        const auto it = m_stats.constFind(c.sha);
        if (it == m_stats.cend())
            return {};
        return QVariantMap{
            {QStringLiteral("files"), it->files},
            {QStringLiteral("added"), it->added},
            {QStringLiteral("removed"), it->removed},
        };
    }
    }
    return {};
}

QHash<int, QByteArray> CommitModel::roleNames() const
{
    return {
        {ShaRole, "sha"},
        {ShortShaRole, "shortSha"},
        {SubjectRole, "subject"},
        {AuthorRole, "author"},
        {AuthorEmailRole, "authorEmail"},
        {InitialsRole, "initials"},
        {DateRole, "date"},
        {RefsRole, "refs"},
        {GraphRole, "graph"},
        {LaneColorRole, "laneColor"},
        {IsHeadRole, "isHead"},
        {IsMergeRole, "isMerge"},
        {PlannedRole, "planned"},
        {StatsRole, "stats"},
        {MatchedRole, "matched"},
        {ReviewRole, "review"},
    };
}

QString CommitModel::shaAt(int row) const
{
    return row >= 0 && row < m_commits.size() ? m_commits[row].sha : QString();
}

int CommitModel::rowOf(const QString &sha) const
{
    return m_rowBySha.value(sha, -1);
}

void CommitModel::setSearch(const QString &query, bool caseSensitive, bool regex)
{
    static const QRegularExpression token(QStringLiteral("(?:(\\w+):)?(?:\"([^\"]*)\"?|(\\S+))"));
    m_terms.clear();
    m_caseSensitive = caseSensitive;
    m_regex = regex;
    m_searchError.clear();

    for (auto it = token.globalMatch(query); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        SearchTerm term;
        term.text = m.captured(2).isNull() ? m.captured(3) : m.captured(2);
        const QString field = m.captured(1).toLower();
        if (field == QLatin1StringView("author"))
            term.field = SearchTerm::Author;
        else if (field == QLatin1StringView("message") || field == QLatin1StringView("msg"))
            term.field = SearchTerm::Message;
        else if (field == QLatin1StringView("sha") || field == QLatin1StringView("commit"))
            term.field = SearchTerm::Sha;
        else if (!field.isEmpty())
            term.text = m.captured(0); // not an operator, e.g. "fix:" in a message
        if (term.text.isEmpty())
            continue;
        if (regex) {
            term.pattern.setPattern(term.text);
            if (!caseSensitive)
                term.pattern.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
            if (!term.pattern.isValid())
                m_searchError = tr("Invalid pattern: %1").arg(term.pattern.errorString());
        }
        m_terms.append(term);
    }

    updateMatches();
    if (count() > 0)
        emit dataChanged(index(0), index(count() - 1), {MatchedRole});
    emit searchChanged();
}

bool CommitModel::termMatches(const SearchTerm &term, const CommitInfo &commit) const
{
    const auto contains = [this, &term](const QString &text) {
        return m_regex ? term.pattern.match(text).hasMatch()
                       : text.contains(term.text, m_caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
    };
    const bool shaMatches = commit.sha.startsWith(term.text.toLower());
    switch (term.field) {
    case SearchTerm::Author: return contains(commit.authorName) || contains(commit.authorEmail);
    case SearchTerm::Message: return contains(commit.subject);
    case SearchTerm::Sha: return shaMatches;
    case SearchTerm::Any: break;
    }
    return contains(commit.subject) || contains(commit.authorName) || contains(commit.authorEmail) || shaMatches;
}

void CommitModel::updateMatches()
{
    m_matched.assign(m_commits.size(), false);
    m_matchCount = 0;
    if (m_terms.isEmpty() || !m_searchError.isEmpty())
        return;
    for (qsizetype row = 0; row < m_commits.size(); ++row) {
        const CommitInfo &commit = m_commits[row];
        const bool all = std::all_of(m_terms.cbegin(), m_terms.cend(),
                                     [&](const SearchTerm &term) { return termMatches(term, commit); });
        m_matched[row] = all;
        m_matchCount += all ? 1 : 0;
    }
}

bool CommitModel::matches(int row) const
{
    return row >= 0 && row < m_matched.size() && m_matched[row];
}

int CommitModel::findNext(int from, bool forward) const
{
    const int n = count();
    for (int step = 1; step <= n; ++step) {
        const int row = ((from + (forward ? step : -step)) % n + n) % n;
        if (matches(row))
            return row;
    }
    return -1;
}

qint64 CommitModel::timestampAt(int row) const
{
    return m_commits[row].authorDate.toSecsSinceEpoch();
}

bool CommitModel::hasRefsAt(int row) const
{
    return m_refs.contains(m_commits[row].sha);
}

QString CommitModel::laneColorAt(int row) const
{
    return laneColorName(m_graph[row].color);
}
