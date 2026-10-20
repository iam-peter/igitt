#pragma once

#include <QByteArray>
#include <QChar>
#include <QList>
#include <QString>

struct StatusEntry
{
    QString path;
    QString origPath; // source of a rename or copy
    QChar index = QLatin1Char('.');
    QChar worktree = QLatin1Char('.');
    bool untracked = false;
    bool conflicted = false;

    bool hasStagedChange() const { return !untracked && !conflicted && index != QLatin1Char('.'); }
    bool hasUnstagedChange() const { return untracked || conflicted || worktree != QLatin1Char('.'); }
};

struct StatusInfo
{
    QString headSha;  // empty on an unborn branch
    QString branch;   // empty when HEAD is detached
    QString upstream;
    int ahead = 0;
    int behind = 0;
    QList<StatusEntry> entries;
};

// Parses `git status --porcelain=v2 -z --branch`.
StatusInfo parseStatus(const QByteArray &out);
