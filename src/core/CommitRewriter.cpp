#include "CommitRewriter.h"

#include <QRegularExpression>

namespace {

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out.left(out.indexOf('\n'))).trimmed();
}

bool isObjectId(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^([0-9a-f]{40}|[0-9a-f]{64})$"));
    return re.match(s).hasMatch();
}

} // namespace

QString CommitRewriter::Commit::subject() const
{
    return QString::fromUtf8(message.left(message.indexOf('\n'))).trimmed();
}

CommitRewriter::CommitRewriter(const GitRunner &git)
    : m_git(git)
{
}

QString CommitRewriter::revParse(const QString &rev) const
{
    const GitResult r = m_git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                   QStringLiteral("-q"), rev});
    return r.ok() ? firstLine(r.out) : QString();
}

bool CommitRewriter::readCommit(const QString &sha, Commit &commit) const
{
    if (sha.isEmpty())
        return false;
    const GitResult r = m_git.run({QStringLiteral("cat-file"), QStringLiteral("commit"), sha});
    if (!r.ok())
        return false;

    commit = Commit{};
    commit.sha = sha;
    const qsizetype split = r.out.indexOf("\n\n");
    const QByteArray headers = split < 0 ? r.out : r.out.left(split);
    commit.message = split < 0 ? QByteArray() : r.out.mid(split + 2);

    for (const QByteArray &line : headers.split('\n')) {
        if (line.startsWith("tree ")) {
            commit.tree = QString::fromLatin1(line.mid(5));
        } else if (line.startsWith("parent ")) {
            commit.parents.append(QString::fromLatin1(line.mid(7)));
        } else if (line.startsWith("encoding ")) {
            commit.encoding = line.mid(9);
        } else if (line.startsWith("author ")) {
            // author Name <email> 1700000000 +0200
            const QByteArray ident = line.mid(7);
            const qsizetype lt = ident.indexOf('<');
            const qsizetype gt = ident.indexOf('>', lt);
            if (lt < 0 || gt < 0)
                return false;
            commit.authorName = ident.left(lt).trimmed();
            commit.authorEmail = ident.mid(lt + 1, gt - lt - 1);
            commit.authorDate = ident.mid(gt + 1).trimmed();
        }
    }
    return !commit.tree.isEmpty();
}

QString CommitRewriter::commitTree(const QString &tree, const QStringList &parents, const Commit &meta,
                               QString &error) const
{
    QStringList args;
    if (!meta.encoding.isEmpty())
        args << QStringLiteral("-c") << QStringLiteral("i18n.commitEncoding=") + QString::fromLatin1(meta.encoding);
    args << QStringLiteral("commit-tree") << tree;
    for (const QString &parent : parents)
        args << QStringLiteral("-p") << parent;

    QProcessEnvironment env;
    env.insert(QStringLiteral("GIT_AUTHOR_NAME"), QString::fromUtf8(meta.authorName));
    env.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), QString::fromUtf8(meta.authorEmail));
    env.insert(QStringLiteral("GIT_AUTHOR_DATE"), QLatin1Char('@') + QString::fromLatin1(meta.authorDate));

    const GitResult r = m_git.run(args, meta.message, env);
    if (!r.ok()) {
        error = tr("Could not recreate commit %1: %2").arg(meta.sha.left(10), r.errorText());
        return {};
    }
    return firstLine(r.out);
}

QString CommitRewriter::wrapTree(const QString &tree, const QStringList &parents, QString &error) const
{
    QStringList args{QStringLiteral("commit-tree"), tree, QStringLiteral("-m"), QStringLiteral("igitt fold")};
    for (const QString &parent : parents)
        args << QStringLiteral("-p") << parent;
    // Fixed identity: these commits only carry trees into merge-tree, and
    // must not fail because the user has no identity configured.
    QProcessEnvironment env;
    for (const char *role : {"AUTHOR", "COMMITTER"}) {
        env.insert(QStringLiteral("GIT_%1_NAME").arg(QLatin1StringView(role)), QStringLiteral("igitt"));
        env.insert(QStringLiteral("GIT_%1_EMAIL").arg(QLatin1StringView(role)), QStringLiteral("igitt@localhost"));
        env.insert(QStringLiteral("GIT_%1_DATE").arg(QLatin1StringView(role)), QStringLiteral("@0 +0000"));
    }
    const GitResult r = m_git.run(args, {}, env);
    if (!r.ok()) {
        error = r.errorText();
        return {};
    }
    return firstLine(r.out);
}

CommitRewriter::Merge CommitRewriter::merge(const QString &base, const QString &ours, const QString &theirs) const
{
    const GitResult r = m_git.run({QStringLiteral("merge-tree"), QStringLiteral("--write-tree"),
                                   QStringLiteral("--name-only"), QStringLiteral("--no-messages"),
                                   QStringLiteral("-z"), QStringLiteral("--merge-base=") + base,
                                   ours, theirs});
    // Exit code 1 means conflicts, but also some errors; only a conflict
    // still prints the resulting tree.
    const QList<QByteArray> fields = r.out.split('\0');
    const QString tree = QString::fromLatin1(fields.value(0)).trimmed();

    Merge merge;
    if (r.exitCode == 0 && isObjectId(tree)) {
        merge.tree = tree;
    } else if (r.exitCode == 1 && isObjectId(tree)) {
        for (qsizetype i = 1; i < fields.size(); ++i) {
            if (!fields[i].isEmpty())
                merge.conflicts.append(QString::fromUtf8(fields[i]));
        }
        if (merge.conflicts.isEmpty())
            merge.conflicts.append(tr("(unknown path)"));
    } else {
        merge.error = tr("git merge-tree failed (git 2.40 or newer is required): %1").arg(r.errorText());
    }
    return merge;
}
