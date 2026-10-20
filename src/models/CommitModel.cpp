#include "CommitModel.h"

#include <QVariantMap>

namespace {

QString kindName(RefInfo::Kind kind)
{
    switch (kind) {
    case RefInfo::LocalBranch: return QStringLiteral("branch");
    case RefInfo::RemoteBranch: return QStringLiteral("remote");
    case RefInfo::Tag: return QStringLiteral("tag");
    case RefInfo::Stash: return QStringLiteral("stash");
    }
    return {};
}

} // namespace

void CommitModel::setCommits(const QList<CommitInfo> &commits, const QList<RefInfo> &refs,
                             const QString &headSha, const QString &currentBranch)
{
    beginResetModel();
    m_commits = commits;
    m_graph = layoutGraph(commits);
    m_headSha = headSha;

    m_rowBySha.clear();
    m_rowBySha.reserve(commits.size());
    for (int i = 0; i < commits.size(); ++i)
        m_rowBySha.insert(commits[i].sha, i);

    m_refs.clear();
    if (!headSha.isEmpty() && currentBranch.isEmpty()) {
        m_refs[headSha].append(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("HEAD")},
            {QStringLiteral("kind"), QStringLiteral("head")},
            {QStringLiteral("current"), true},
        });
    }
    for (const RefInfo &ref : refs) {
        m_refs[ref.sha].append(QVariantMap{
            {QStringLiteral("name"), ref.name},
            {QStringLiteral("kind"), kindName(ref.kind)},
            {QStringLiteral("current"), ref.kind == RefInfo::LocalBranch && ref.name == currentBranch},
        });
    }
    endResetModel();
}

int CommitModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_commits.size());
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
    case DateRole: return c.authorDate.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    case RefsRole: return m_refs.value(c.sha);
    case GraphRole: return QVariant::fromValue(m_graph[index.row()]);
    case IsHeadRole: return c.sha == m_headSha;
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
        {DateRole, "date"},
        {RefsRole, "refs"},
        {GraphRole, "graph"},
        {IsHeadRole, "isHead"},
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
