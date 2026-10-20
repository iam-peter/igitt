#pragma once

#include "GitRunner.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>

// The object-level steps FoldEngine and HistoryEditor rewrite history
// with: read a commit, merge trees with merge-tree, write a commit with
// commit-tree. None of them touch the index, the working tree or a ref.
class CommitRewriter
{
    Q_DECLARE_TR_FUNCTIONS(CommitRewriter)

public:
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

    explicit CommitRewriter(const GitRunner &git);

    QString revParse(const QString &rev) const;
    bool readCommit(const QString &sha, Commit &commit) const;
    // Like a rebase: keeps author and message, the committer becomes the
    // current user.
    QString commitTree(const QString &tree, const QStringList &parents, const Commit &meta,
                       QString &error) const;
    // A throwaway commit carrying `tree`, for merge-tree in git < 2.45,
    // which wants commits.
    QString wrapTree(const QString &tree, const QStringList &parents, QString &error) const;
    Merge merge(const QString &base, const QString &ours, const QString &theirs) const;

private:
    const GitRunner &m_git;
};
