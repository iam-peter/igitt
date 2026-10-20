#include "HistoryEditor.h"

#include "FoldEngine.h"
#include "MessageFormat.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace {

const QString ChangeIdKey = QStringLiteral("Change-Id");

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out.left(out.indexOf('\n'))).trimmed();
}

bool isChangeId(const MessageFormat::Trailer &trailer)
{
    return trailer.key.compare(ChangeIdKey, Qt::CaseInsensitive) == 0;
}

bool hasChangeId(const MessageFormat::Parts &parts)
{
    return std::any_of(parts.trailers.cbegin(), parts.trailers.cend(), isChangeId);
}

} // namespace

HistoryEditor::HistoryEditor(const GitRunner &git)
    : m_git(git)
    , m_objects(git)
{
}

QString HistoryEditor::blocker(const QString &commit, const QString &head) const
{
    // The same restrictions as folding: on the branch, no merges above.
    return FoldEngine(m_git).blocker(commit, head);
}

QList<RewriteStep> HistoryEditor::todo(const QString &oldest, const QString &head, QString &error) const
{
    CommitRewriter::Commit commit;
    if (!m_objects.readCommit(m_objects.revParse(oldest + QStringLiteral("^{commit}")), commit)) {
        error = tr("%1 is not a commit.").arg(oldest);
        return {};
    }
    QStringList args{QStringLiteral("rev-list"), QStringLiteral("--reverse"), QStringLiteral("--first-parent"),
                     head};
    if (!commit.parents.isEmpty())
        args << QStringLiteral("--not") << commit.parents;
    const GitResult listed = m_git.run(args);
    if (!listed.ok()) {
        error = listed.errorText();
        return {};
    }
    const QStringList shas = QString::fromLatin1(listed.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (shas.value(0) != commit.sha) {
        error = tr("The commit is not part of the current branch.");
        return {};
    }
    QList<RewriteStep> steps;
    for (const QString &sha : shas)
        steps.append({RewriteStep::Pick, sha, {}});
    return steps;
}

QList<RewriteStep> HistoryEditor::todo(const HistoryEdit &edit, const QString &head, QString &error) const
{
    const QString sha = m_objects.revParse(edit.commit + QStringLiteral("^{commit}"));
    CommitRewriter::Commit commit;
    if (!m_objects.readCommit(sha, commit)) {
        error = tr("%1 is not a commit.").arg(edit.commit);
        return {};
    }

    QList<RewriteStep> steps;
    switch (edit.kind) {
    case HistoryEdit::Reword:
        steps = todo(sha, head, error);
        if (!steps.isEmpty()) {
            steps.first().action = RewriteStep::Reword;
            steps.first().message = edit.message;
        }
        break;
    case HistoryEdit::Drop:
        steps = todo(sha, head, error);
        if (steps.size() == 1 && commit.parents.isEmpty()) {
            error = tr("Dropping the only commit would leave the branch empty.");
            return {};
        }
        if (!steps.isEmpty())
            steps.first().action = RewriteStep::Drop;
        break;
    case HistoryEdit::Squash:
    case HistoryEdit::Fixup:
        if (commit.parents.isEmpty()) {
            error = tr("The first commit has no commit before it to be combined with.");
            return {};
        }
        steps = todo(commit.parents.first(), head, error);
        if (steps.size() >= 2)
            steps[1].action = edit.kind == HistoryEdit::Squash ? RewriteStep::Squash : RewriteStep::Fixup;
        break;
    case HistoryEdit::Move: {
        const QString to = m_objects.revParse(edit.to + QStringLiteral("^{commit}"));
        if (to.isEmpty()) {
            error = tr("%1 is not a commit.").arg(edit.to);
            return {};
        }
        if (to == sha) {
            error = tr("The commit is already there.");
            return {};
        }
        // Start at whichever of the two is older.
        steps = todo(sha, head, error);
        const auto indexOf = [&steps](const QString &id) {
            for (qsizetype i = 0; i < steps.size(); ++i) {
                if (steps[i].commit == id)
                    return i;
            }
            return qsizetype(-1);
        };
        if (!steps.isEmpty() && indexOf(to) < 0) {
            error.clear();
            steps = todo(to, head, error);
        }
        const qsizetype from = indexOf(sha);
        const qsizetype into = indexOf(to);
        if (from < 0 || into < 0) {
            if (error.isEmpty())
                error = tr("The commit is not part of the current branch.");
            return {};
        }
        steps.move(from, into);
        break;
    }
    }
    return error.isEmpty() ? steps : QList<RewriteStep>();
}

RewriteResult HistoryEditor::rewrite(const QList<RewriteStep> &steps, const QString &head, bool moveHead,
                                     const QString &reflog) const
{
    RewriteResult result;
    const auto fail = [&result](const QString &error) {
        result.error = error;
        return result;
    };
    if (steps.isEmpty())
        return fail(tr("Nothing to do."));

    result.oldHead = m_objects.revParse(head + QStringLiteral("^{commit}"));
    if (result.oldHead.isEmpty())
        return fail(tr("The branch has no commits yet."));

    // The steps must be exactly the newest commits of the chain.
    const GitResult listed = m_git.run({QStringLiteral("rev-list"), QStringLiteral("--first-parent"),
                                        QStringLiteral("--max-count=%1").arg(steps.size()), result.oldHead});
    if (!listed.ok())
        return fail(listed.errorText());
    const QStringList chain = QString::fromLatin1(listed.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QSet<QString> named;
    for (const RewriteStep &step : steps)
        named.insert(m_objects.revParse(step.commit + QStringLiteral("^{commit}")));
    if (chain.size() != steps.size() || named != QSet<QString>(chain.cbegin(), chain.cend()))
        return fail(tr("The commits to edit must be the latest ones of the current branch, each named once."));
    if (const QString reason = blocker(chain.last(), result.oldHead); !reason.isEmpty())
        return fail(reason);

    CommitRewriter::Commit oldest;
    if (!m_objects.readCommit(chain.last(), oldest))
        return fail(tr("Could not read commit %1.").arg(chain.last()));

    // Where the root commit's changes come from, or go onto once it is
    // dropped or moved: an empty tree, wrapped for merge-tree.
    QString emptyCommit;
    QString error;
    const auto orEmpty = [&](const QString &commit) {
        if (!commit.isEmpty())
            return commit;
        if (emptyCommit.isEmpty()) {
            const QString emptyTree = firstLine(m_git.run({QStringLiteral("hash-object"), QStringLiteral("-t"),
                                                           QStringLiteral("tree"), QStringLiteral("-w"),
                                                           QStringLiteral("--stdin")}).out);
            emptyCommit = m_objects.wrapTree(emptyTree, {}, error);
        }
        return emptyCommit;
    };

    QString newParent = oldest.parents.value(0);
    CommitRewriter::Commit tip; // the commit written last, as written
    QStringList tipSources;     // the original commits that went into it
    QHash<QString, QString> rewritten;
    QStringList order;

    for (const RewriteStep &step : steps) {
        CommitRewriter::Commit commit;
        if (!m_objects.readCommit(m_objects.revParse(step.commit + QStringLiteral("^{commit}")), commit))
            return fail(tr("Could not read commit %1.").arg(step.commit));
        const QString shortSha = commit.sha.left(10);
        order.append(commit.sha);

        if (step.action == RewriteStep::Drop) {
            rewritten.insert(commit.sha, QString());
            continue;
        }
        const bool combine = step.action == RewriteStep::Squash || step.action == RewriteStep::Fixup;
        if (combine && tip.sha.isEmpty())
            return fail(tr("%1 \"%2\" has no commit before it to be combined with.")
                            .arg(shortSha, commit.subject()));

        // The commit's change, on top of whatever is below it now.
        const QString oldParent = commit.parents.value(0);
        QString tree;
        QStringList parents = newParent.isEmpty() ? QStringList() : QStringList{newParent};
        if (commit.parents.size() > 1) {
            if (combine || newParent != oldParent)
                return fail(tr("%1 \"%2\" is a merge commit; it can be reworded, but not moved or combined.")
                                .arg(shortSha, commit.subject()));
            tree = commit.tree;
            parents = commit.parents;
        } else if (newParent == oldParent) {
            tree = commit.tree;
        } else {
            const QString base = orEmpty(oldParent);
            const QString ours = orEmpty(newParent);
            if (!error.isEmpty())
                return fail(error);
            const CommitRewriter::Merge merged = m_objects.merge(base, ours, commit.sha);
            if (!merged.error.isEmpty())
                return fail(merged.error);
            if (!merged.conflicts.isEmpty()) {
                result.conflicts = merged.conflicts;
                return fail(tr("Replaying %1 \"%2\" in its new place conflicts; it probably depends on "
                               "a commit that now comes after it.").arg(shortSha, commit.subject()));
            }
            tree = merged.tree;
        }

        if (combine) {
            CommitRewriter::Commit meta = tip;
            if (step.action == RewriteStep::Squash) {
                const QString combined = step.message.isEmpty()
                    ? squashMessage(QString::fromUtf8(tip.message), QString::fromUtf8(commit.message))
                    : keepChangeId(step.message, QString::fromUtf8(tip.message));
                meta.message = combined.toUtf8();
                meta.encoding.clear();
            }
            const QString sha = m_objects.commitTree(tree, tip.parents, meta, error);
            if (!error.isEmpty())
                return fail(error);
            tipSources.append(commit.sha);
            for (const QString &source : std::as_const(tipSources))
                rewritten.insert(source, sha);
            tip = meta;
            tip.sha = sha;
            tip.tree = tree;
            newParent = sha;
            continue;
        }

        CommitRewriter::Commit meta = commit;
        if (step.action == RewriteStep::Reword) {
            if (MessageFormat::split(step.message).title.isEmpty())
                return fail(tr("The new message for %1 is empty.").arg(shortSha));
            const QString message = keepChangeId(step.message, QString::fromUtf8(commit.message));
            meta.message = message.toUtf8();
            if (meta.message != commit.message)
                meta.encoding.clear();
        }
        QString sha = commit.sha;
        if (parents != commit.parents || meta.message != commit.message) {
            sha = m_objects.commitTree(tree, parents, meta, error);
            if (!error.isEmpty())
                return fail(error);
            rewritten.insert(commit.sha, sha);
        }
        tip = meta;
        tip.sha = sha;
        tip.tree = tree;
        tip.parents = parents;
        tipSources = {commit.sha};
        newParent = sha;
    }

    if (newParent.isEmpty())
        return fail(tr("Dropping every commit would leave the branch empty."));
    result.newHead = newParent;
    QSet<QString> created;
    for (const QString &sha : std::as_const(order)) {
        if (const auto it = rewritten.constFind(sha); it != rewritten.cend()) {
            result.rewrites.append({sha, *it});
            if (!it->isEmpty())
                created.insert(*it);
        }
    }
    result.rewritten = int(created.size());
    if (result.newHead == result.oldHead) {
        result.ok = true;
        return result;
    }

    // 2. Publish: only succeeds if HEAD is still where we started.
    if (moveHead) {
        const GitResult moved = m_git.run({QStringLiteral("update-ref"), QStringLiteral("-m"),
                                           reflog.isEmpty() ? QStringLiteral("igitt: edit history") : reflog,
                                           QStringLiteral("HEAD"), result.newHead, result.oldHead});
        if (!moved.ok())
            return fail(tr("HEAD moved while editing the history; nothing was changed. %1")
                            .arg(moved.errorText()));
    }
    result.ok = true;

    // 3. Paths staged as HEAD had them follow the new HEAD; anything else
    // that was staged stays as it was.
    if (m_objects.revParse(result.oldHead + QStringLiteral("^{tree}"))
        != m_objects.revParse(result.newHead + QStringLiteral("^{tree}"))) {
        const GitResult carried = m_git.run({QStringLiteral("read-tree"), QStringLiteral("-m"), QStringLiteral("-i"),
                                             result.oldHead, result.newHead});
        result.indexUpdated = carried.ok();
        if (!carried.ok())
            result.error = tr("The history was edited, but the index could not follow: %1")
                               .arg(carried.errorText());
    }
    return result;
}

RewriteResult HistoryEditor::edit(const HistoryEdit &edit, const QString &head, bool moveHead) const
{
    QString error;
    const QList<RewriteStep> steps = todo(edit, head, error);
    if (steps.isEmpty()) {
        RewriteResult result;
        result.error = error.isEmpty() ? tr("Nothing to do.") : error;
        return result;
    }
    return rewrite(steps, head, moveHead,
                   QStringLiteral("igitt: %1 %2").arg(verb(edit.kind), m_objects.revParse(edit.commit).left(10)));
}

QString HistoryEditor::verb(HistoryEdit::Kind kind)
{
    switch (kind) {
    case HistoryEdit::Reword: return QStringLiteral("reword");
    case HistoryEdit::Move: return QStringLiteral("move");
    case HistoryEdit::Squash: return QStringLiteral("squash");
    case HistoryEdit::Fixup: return QStringLiteral("fixup");
    case HistoryEdit::Drop: return QStringLiteral("drop");
    }
    return {};
}

QString HistoryEditor::keepChangeId(const QString &message, const QString &original)
{
    MessageFormat::Parts parts = MessageFormat::split(message);
    if (!hasChangeId(parts)) {
        for (const MessageFormat::Trailer &trailer : MessageFormat::split(original).trailers) {
            if (isChangeId(trailer))
                parts.trailers.append(trailer);
        }
    }
    return MessageFormat::join(parts);
}

QString HistoryEditor::squashMessage(const QString &first, const QString &second)
{
    MessageFormat::Parts parts = MessageFormat::split(first);
    const MessageFormat::Parts added = MessageFormat::split(second);
    QStringList body;
    for (const QString &block : {parts.body, added.title, added.body}) {
        if (!block.trimmed().isEmpty())
            body.append(block.trimmed());
    }
    parts.body = body.join(QStringLiteral("\n\n"));
    // Gerrit knows the result as the first commit's change.
    const bool changeId = hasChangeId(parts);
    for (const MessageFormat::Trailer &trailer : added.trailers) {
        if (!parts.trailers.contains(trailer) && !(changeId && isChangeId(trailer)))
            parts.trailers.append(trailer);
    }
    return MessageFormat::join(parts);
}
