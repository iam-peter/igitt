#pragma once

#include "GitRunner.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>

// What a patch passed to FoldEngine::fold() was made against.
enum class FoldBase {
    Head,  // from the staged diff; the index stays as it is
    Index, // from the unstaged diff; the folded lines are staged afterwards
};

struct FoldResult
{
    bool ok = false;
    QString error;
    QStringList conflicts; // paths, when a merge step conflicted
    QString oldHead;
    QString newHead;
    QString newTarget;
    int rewritten = 0; // commits recreated, the target included
    QList<std::pair<QString, QString>> rewrites; // old and new id of each
    bool indexUpdated = false;
};

// Folds a patch into an earlier commit of the current branch, the in-memory
// equivalent of `git commit --fixup=<target>` followed by
// `git rebase -i --autosquash <target>~`:
//
//  1. Apply the patch to its base tree in a temporary index.
//  2. Merge that change into the target commit with merge-tree, using the
//     base tree as merge base, and recreate the target with the result.
//  3. Replay every commit from the target up to HEAD the same way.
//  4. Move HEAD (compare-and-swap), then stage the folded lines if they
//     came from the unstaged diff.
//
// The working tree is never touched, and nothing visible changes before
// step 4, so a conflict at any earlier step leaves the repository as it was.
class FoldEngine
{
    Q_DECLARE_TR_FUNCTIONS(FoldEngine)

public:
    explicit FoldEngine(const GitRunner &git);

    // Why `target` cannot be folded into, or an empty string if it can.
    // `head` is the commit to treat as HEAD, for simulations.
    QString blocker(const QString &target, const QString &head = QStringLiteral("HEAD")) const;

    // With `moveHead` false, step 4 leaves HEAD alone (the index is still
    // updated) and the caller takes result.newHead from there.
    FoldResult fold(const QString &target, const QByteArray &patch, FoldBase base,
                    const QString &head = QStringLiteral("HEAD"), bool moveHead = true) const;

private:
    struct Commit
    {
        QString sha;
        QString tree;
        QStringList parents;
        QByteArray authorName;
        QByteArray authorEmail;
        QByteArray authorDate;
        QByteArray encoding;
        QByteArray message;
        QString subject() const;
    };

    struct Merge
    {
        QString tree;
        QStringList conflicts;
        QString error;
    };

    QString revParse(const QString &rev) const;
    bool readCommit(const QString &sha, Commit &commit) const;
    QString commitTree(const QString &tree, const QStringList &parents, const Commit &meta,
                       QString &error) const;
    QString wrapTree(const QString &tree, const QStringList &parents, QString &error) const;
    Merge merge(const QString &base, const QString &ours, const QString &theirs) const;

    const GitRunner &m_git;
};
