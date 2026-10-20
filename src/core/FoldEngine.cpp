#include "FoldEngine.h"

#include "MergeConflict.h"

#include <QTemporaryDir>

namespace {

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out.left(out.indexOf('\n'))).trimmed();
}

} // namespace

FoldEngine::FoldEngine(const GitRunner &git)
    : m_git(git)
    , m_objects(git)
{
}

QString FoldEngine::blocker(const QString &target, const QString &headRev) const
{
    if (const QString operation = operationInProgress(m_git); !operation.isEmpty())
        return tr("git is in the middle of an operation (%1); finish or abort it first.").arg(operation);
    const QString head = m_objects.revParse(headRev);
    if (head.isEmpty())
        return tr("The branch has no commits yet.");
    const QString sha = m_objects.revParse(target + QStringLiteral("^{commit}"));
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

FoldResult FoldEngine::fold(const QString &target, const QByteArray &patch, FoldBase base,
                            const QString &head, bool moveHead) const
{
    FoldResult result;
    const auto fail = [&result](const QString &error) {
        result.error = error;
        return result;
    };

    if (patch.isEmpty())
        return fail(tr("Nothing is selected."));
    if (const QString reason = blocker(target, head); !reason.isEmpty())
        return fail(reason);

    result.oldHead = m_objects.revParse(head);
    CommitRewriter::Commit targetCommit;
    if (!m_objects.readCommit(m_objects.revParse(target + QStringLiteral("^{commit}")), targetCommit))
        return fail(tr("Could not read commit %1.").arg(target));

    // 1. The change as a pair of trees: its base and the base with the patch.
    QString baseTree;
    if (base == FoldBase::Head) {
        baseTree = m_objects.revParse(result.oldHead + QStringLiteral("^{tree}"));
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
    // --3way: in a plan, the patch may stem from a state that earlier
    // entries no longer produce; merging with its preimage still works.
    if (const GitResult r = m_git.run({QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("--3way"),
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
    const QString baseCommit = base == FoldBase::Head ? result.oldHead : m_objects.wrapTree(baseTree, {}, error);
    const QString changeCommit = error.isEmpty() ? m_objects.wrapTree(changedTree, {baseCommit}, error) : QString();
    if (!error.isEmpty())
        return fail(error);

    // 2. Fold the change into the target.
    const QString shortTarget = targetCommit.sha.left(10);
    CommitRewriter::Merge merged = m_objects.merge(baseCommit, targetCommit.sha, changeCommit);
    if (!merged.error.isEmpty())
        return fail(merged.error);
    if (!merged.conflicts.isEmpty()) {
        result.conflicts = merged.conflicts;
        return fail(tr("The selected lines conflict with %1 \"%2\"; they probably build on changes "
                       "made by later commits.").arg(shortTarget, targetCommit.subject()));
    }
    result.newTarget = m_objects.commitTree(merged.tree, targetCommit.parents, targetCommit, error);
    if (!error.isEmpty())
        return fail(error);
    result.rewritten = 1;
    result.rewrites.append({targetCommit.sha, result.newTarget});

    // 3. Replay everything after the target onto the new commit.
    const GitResult descendants = m_git.run({QStringLiteral("rev-list"), QStringLiteral("--reverse"),
                                             targetCommit.sha + QStringLiteral("..") + result.oldHead});
    if (!descendants.ok())
        return fail(descendants.errorText());

    QString oldParent = targetCommit.sha;
    QString newParent = result.newTarget;
    const QStringList shas = QString::fromLatin1(descendants.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &sha : shas) {
        CommitRewriter::Commit commit;
        if (!m_objects.readCommit(sha, commit))
            return fail(tr("Could not read commit %1.").arg(sha));
        merged = m_objects.merge(oldParent, newParent, sha);
        if (!merged.error.isEmpty())
            return fail(merged.error);
        if (!merged.conflicts.isEmpty()) {
            result.conflicts = merged.conflicts;
            return fail(tr("Replaying %1 \"%2\" on top of the folded commit conflicts.")
                            .arg(sha.left(10), commit.subject()));
        }
        newParent = m_objects.commitTree(merged.tree, {newParent}, commit, error);
        if (!error.isEmpty())
            return fail(error);
        result.rewrites.append({sha, newParent});
        oldParent = sha;
        ++result.rewritten;
    }
    result.newHead = newParent;

    // 4. Publish: only succeeds if HEAD is still where we started.
    if (moveHead) {
        const GitResult moved = m_git.run({QStringLiteral("update-ref"), QStringLiteral("-m"),
                                           QStringLiteral("igitt: fold into %1").arg(shortTarget),
                                           QStringLiteral("HEAD"), result.newHead, result.oldHead});
        if (!moved.ok())
            return fail(tr("HEAD moved while folding; nothing was changed. %1").arg(moved.errorText()));
    }
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
