#include "FoldEngine.h"

#include <QRegularExpression>
#include <QTemporaryDir>

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

QString FoldEngine::Commit::subject() const
{
    return QString::fromUtf8(message.left(message.indexOf('\n'))).trimmed();
}

FoldEngine::FoldEngine(const GitRunner &git)
    : m_git(git)
{
}

QString FoldEngine::blocker(const QString &target) const
{
    const QString head = revParse(QStringLiteral("HEAD"));
    if (head.isEmpty())
        return tr("The branch has no commits yet.");
    const QString sha = revParse(target + QStringLiteral("^{commit}"));
    if (sha.isEmpty())
        return tr("%1 is not a commit.").arg(target);

    const GitResult ancestor = m_git.run({QStringLiteral("merge-base"), QStringLiteral("--is-ancestor"), sha, head});
    if (ancestor.exitCode == 1)
        return tr("The commit is not part of the current branch.");
    if (!ancestor.ok())
        return ancestor.errorText();

    const GitResult merges = m_git.run({QStringLiteral("rev-list"), QStringLiteral("--count"),
                                        QStringLiteral("--min-parents=2"), sha + QStringLiteral("..") + head});
    if (firstLine(merges.out).toInt() > 0)
        return tr("There are merge commits between this commit and HEAD; replaying merges is not supported yet.");
    return {};
}

FoldResult FoldEngine::fold(const QString &target, const QByteArray &patch, FoldBase base) const
{
    FoldResult result;
    const auto fail = [&result](const QString &error) {
        result.error = error;
        return result;
    };

    if (patch.isEmpty())
        return fail(tr("Nothing is selected."));
    if (const QString reason = blocker(target); !reason.isEmpty())
        return fail(reason);

    result.oldHead = revParse(QStringLiteral("HEAD"));
    Commit targetCommit;
    if (!readCommit(revParse(target + QStringLiteral("^{commit}")), targetCommit))
        return fail(tr("Could not read commit %1.").arg(target));

    // 1. The change as a pair of trees: its base and the base with the patch.
    QString baseTree;
    if (base == FoldBase::Head) {
        baseTree = revParse(QStringLiteral("HEAD^{tree}"));
    } else {
        const GitResult written = m_git.run({QStringLiteral("write-tree")});
        if (!written.ok())
            return fail(tr("The index has unresolved conflicts."));
        baseTree = firstLine(written.out);
    }

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return fail(tr("Could not create a temporary directory."));
    QProcessEnvironment tmpIndex;
    tmpIndex.insert(QStringLiteral("GIT_INDEX_FILE"), tmp.filePath(QStringLiteral("index")));

    if (const GitResult r = m_git.run({QStringLiteral("read-tree"), baseTree}, {}, tmpIndex); !r.ok())
        return fail(r.errorText());
    if (const GitResult r = m_git.run({QStringLiteral("apply"), QStringLiteral("--cached"),
                                       QStringLiteral("--whitespace=nowarn")}, patch, tmpIndex);
        !r.ok())
        return fail(tr("The selected lines do not apply: %1").arg(r.errorText()));
    const GitResult changed = m_git.run({QStringLiteral("write-tree")}, {}, tmpIndex);
    if (!changed.ok())
        return fail(changed.errorText());
    const QString changedTree = firstLine(changed.out);
    if (changedTree == baseTree)
        return fail(tr("The selection contains no changes."));

    // merge-tree in git < 2.45 wants commits, so wrap the trees in
    // throwaway ones; nothing refers to them afterwards.
    QString error;
    const QString baseCommit = base == FoldBase::Head ? result.oldHead : wrapTree(baseTree, {}, error);
    const QString changeCommit = error.isEmpty() ? wrapTree(changedTree, {baseCommit}, error) : QString();
    if (!error.isEmpty())
        return fail(error);

    // 2. Fold the change into the target.
    const QString shortTarget = targetCommit.sha.left(10);
    Merge merged = merge(baseCommit, targetCommit.sha, changeCommit);
    if (!merged.error.isEmpty())
        return fail(merged.error);
    if (!merged.conflicts.isEmpty()) {
        result.conflicts = merged.conflicts;
        return fail(tr("The selected lines conflict with %1 \"%2\"; they probably build on changes "
                       "made by later commits.").arg(shortTarget, targetCommit.subject()));
    }
    result.newTarget = commitTree(merged.tree, targetCommit.parents, targetCommit, error);
    if (!error.isEmpty())
        return fail(error);
    result.rewritten = 1;

    // 3. Replay everything after the target onto the new commit.
    const GitResult descendants = m_git.run({QStringLiteral("rev-list"), QStringLiteral("--reverse"),
                                             targetCommit.sha + QStringLiteral("..") + result.oldHead});
    if (!descendants.ok())
        return fail(descendants.errorText());

    QString oldParent = targetCommit.sha;
    QString newParent = result.newTarget;
    const QStringList shas = QString::fromLatin1(descendants.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &sha : shas) {
        Commit commit;
        if (!readCommit(sha, commit))
            return fail(tr("Could not read commit %1.").arg(sha));
        merged = merge(oldParent, newParent, sha);
        if (!merged.error.isEmpty())
            return fail(merged.error);
        if (!merged.conflicts.isEmpty()) {
            result.conflicts = merged.conflicts;
            return fail(tr("Replaying %1 \"%2\" on top of the folded commit conflicts.")
                            .arg(sha.left(10), commit.subject()));
        }
        newParent = commitTree(merged.tree, {newParent}, commit, error);
        if (!error.isEmpty())
            return fail(error);
        oldParent = sha;
        ++result.rewritten;
    }
    result.newHead = newParent;

    // 4. Publish: only succeeds if HEAD is still where we started.
    const GitResult moved = m_git.run({QStringLiteral("update-ref"), QStringLiteral("-m"),
                                       QStringLiteral("igitt: fold into %1").arg(shortTarget),
                                       QStringLiteral("HEAD"), result.newHead, result.oldHead});
    if (!moved.ok())
        return fail(tr("HEAD moved while folding; nothing was changed. %1").arg(moved.errorText()));
    result.ok = true;

    if (base == FoldBase::Index) {
        // The folded lines are part of HEAD now; stage them so they don't
        // show up as unstaged changes reverting nothing.
        const GitResult staged = m_git.run({QStringLiteral("apply"), QStringLiteral("--cached"),
                                            QStringLiteral("--whitespace=nowarn")}, patch);
        result.indexUpdated = staged.ok();
        if (!staged.ok())
            result.error = tr("The commits were rewritten, but staging the folded lines failed: %1")
                               .arg(staged.errorText());
    }
    return result;
}

QString FoldEngine::revParse(const QString &rev) const
{
    const GitResult r = m_git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                   QStringLiteral("-q"), rev});
    return r.ok() ? firstLine(r.out) : QString();
}

bool FoldEngine::readCommit(const QString &sha, Commit &commit) const
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

QString FoldEngine::commitTree(const QString &tree, const QStringList &parents, const Commit &meta,
                               QString &error) const
{
    // Like a rebase: keep author and message, the committer becomes the
    // current user.
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

QString FoldEngine::wrapTree(const QString &tree, const QStringList &parents, QString &error) const
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

FoldEngine::Merge FoldEngine::merge(const QString &base, const QString &ours, const QString &theirs) const
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
