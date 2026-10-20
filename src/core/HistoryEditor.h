#pragma once

#include "CommitRewriter.h"
#include "GitRunner.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

// One line of an interactive rebase's todo list.
struct RewriteStep
{
    enum Action { Pick, Reword, Squash, Fixup, Drop };

    Action action = Pick;
    QString commit;
    // Reword: the new message. Squash: the combined message; empty to
    // combine both messages.
    QString message;
};

// An edit as the history view offers it; HistoryEditor turns it into
// the todo list for the commits it affects.
struct HistoryEdit
{
    enum Kind {
        Reword, // give `commit` a new message
        Move,   // put `commit` where `to` is, shifting the commits between
        Squash, // combine `commit` with its parent, keeping both messages
        Fixup,  // like Squash, keeping only the parent's message
        Drop,
    };

    Kind kind = Reword;
    QString commit;
    QString to;      // Move
    QString message; // Reword
};

struct RewriteResult
{
    bool ok = false;
    QString error;
    QStringList conflicts; // paths, when replaying a commit conflicted
    QString oldHead;
    QString newHead;
    int rewritten = 0; // commits recreated
    // The old and new id of every recreated commit; a squashed commit maps
    // to the one it went into, a dropped one to an empty string.
    QList<std::pair<QString, QString>> rewrites;
    bool indexUpdated = false;
};

// Edits the first-parent chain of the current branch like `git rebase -i`,
// without touching the working tree:
//
//  1. Replay the todo list in order, each commit as a merge-tree of its
//     original parent, the new parent and itself; commits whose parent
//     did not change and that keep their message are kept as they are.
//  2. Move HEAD (compare-and-swap).
//  3. Carry the index over to the new HEAD with a two-tree read-tree, so
//     what was staged stays staged, and the changes a dropped commit made
//     show up as unstaged changes in the untouched working tree.
//
// Author, date and message are kept unless the step changes the message;
// Change-Id trailers always are. A conflict at any step leaves the
// repository as it was.
class HistoryEditor
{
    Q_DECLARE_TR_FUNCTIONS(HistoryEditor)

public:
    explicit HistoryEditor(const GitRunner &git);

    // Why `commit` cannot be edited, or an empty string if it can. `head`
    // is the commit to treat as HEAD, for simulations.
    QString blocker(const QString &commit, const QString &head = QStringLiteral("HEAD")) const;

    // The commits from `oldest` up to `head`, oldest first, each picked.
    QList<RewriteStep> todo(const QString &oldest, const QString &head, QString &error) const;
    // The todo list `edit` amounts to.
    QList<RewriteStep> todo(const HistoryEdit &edit, const QString &head, QString &error) const;

    // Replays `steps` in their order on top of the parent of the oldest
    // commit they name; they must name every commit from there up to
    // `head` once, as todo() lists them. With `moveHead` false, HEAD stays
    // where it is (the index is still updated) and the caller takes
    // result.newHead from there.
    RewriteResult rewrite(const QList<RewriteStep> &steps, const QString &head = QStringLiteral("HEAD"),
                          bool moveHead = true, const QString &reflog = {}) const;
    RewriteResult edit(const HistoryEdit &edit, const QString &head = QStringLiteral("HEAD"),
                       bool moveHead = true) const;

    // "reword", "move", ...: for logs.
    static QString verb(HistoryEdit::Kind kind);
    // `message` with `original`'s Change-Id added back if it lost it, in
    // one trailer block.
    static QString keepChangeId(const QString &message, const QString &original);
    // The message `git rebase -i` offers for a squash: both messages, with
    // the trailers of both in one block and only `first`'s Change-Id.
    static QString squashMessage(const QString &first, const QString &second);

private:
    const GitRunner &m_git;
    CommitRewriter m_objects;
};
