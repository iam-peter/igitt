#include "ChangeMove.h"

#include "CommitRewriter.h"
#include "PatchCommit.h"

#include <QCoreApplication>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ChangeMove", text);
}

// The id after the rewrites, or the id itself if they left it alone
QString rewritten(const QList<std::pair<QString, QString>> &rewrites, const QString &id)
{
    for (const auto &[from, to] : rewrites) {
        if (from == id)
            return to;
    }
    return id;
}

} // namespace

FoldResult moveChange(const GitRunner &git, const QString &from, const QString &to, const QByteArray &patch,
                      const QString &head, bool moveHead)
{
    FoldResult result;
    const auto fail = [&result](const QString &error) {
        result.error = error;
        return result;
    };
    if (patch.isEmpty())
        return fail(tr("There is no change to move."));
    if (from == to)
        return fail(tr("The change is there already."));

    const CommitRewriter objects(git);
    const FoldEngine engine(git);
    const QString oldHead = objects.revParse(head);
    if (oldHead.isEmpty())
        return fail(tr("The branch has no commits yet."));
    QString error;

    // What the change was made on, and the change on it
    const QString source = from.isEmpty() ? oldHead : objects.revParse(from);
    if (source.isEmpty())
        return fail(tr("%1 is not a commit.").arg(from));
    const QString parent = from.isEmpty() ? oldHead : objects.revParse(source + QStringLiteral("^"));

    // The change as two trees: what it was made on, and that with it. A
    // root commit's change was made on nothing, the empty tree.
    const QString madeOn = parent.isEmpty()
        ? QString::fromLatin1(git.run({QStringLiteral("mktree")}, QByteArray()).out).trimmed()
        : objects.revParse(parent + QStringLiteral("^{tree}"));
    // --3way, as for a fold: in a plan, the patch may stem from a state
    // that earlier entries no longer produce
    const QString with = treeWithPatch(git, madeOn, patch, error, true);
    if (with.isEmpty())
        return fail(tr("The change does not apply where it was made: %1").arg(error));

    QString current = oldHead;
    if (!from.isEmpty()) {
        // Out of its commit: the step back from the change to what it was
        // made on, merged into the commit, takes back only the change
        result = engine.foldChange(source, with, madeOn, current, false);
        if (!result.ok)
            return result;
        current = result.newHead;
    }
    if (!to.isEmpty()) {
        // Into the other commit, where the first step may have put it
        const QString target = rewritten(result.rewrites, objects.revParse(to));
        if (target.isEmpty())
            return fail(tr("%1 is not a commit.").arg(to));
        const FoldResult into = engine.foldChange(target, madeOn, with, current, false);
        if (!into.ok)
            return into;
        // One list of what became of each commit, across both steps
        QList<std::pair<QString, QString>> rewrites;
        for (const auto &[old, mid] : result.rewrites)
            rewrites.append({old, rewritten(into.rewrites, mid)});
        for (const auto &[mid, now] : into.rewrites) {
            const bool known = std::any_of(result.rewrites.cbegin(), result.rewrites.cend(),
                                           [&mid](const auto &step) { return step.second == mid; });
            if (!known)
                rewrites.append({mid, now});
        }
        result.ok = true;
        result.error.clear();
        result.conflicts.clear();
        result.newTarget = into.newTarget;
        result.newHead = into.newHead;
        result.rewrites = rewrites;
        result.rewritten = rewrites.size();
    }
    result.oldHead = oldHead;

    if (moveHead) {
        const GitResult moved = git.run({QStringLiteral("update-ref"), QStringLiteral("-m"),
                                         QStringLiteral("igitt: move a change"), QStringLiteral("HEAD"),
                                         result.newHead, oldHead});
        if (!moved.ok()) {
            result.ok = false;
            return fail(tr("HEAD moved meanwhile; nothing was changed. %1").arg(moved.errorText()));
        }
    }
    return result;
}
