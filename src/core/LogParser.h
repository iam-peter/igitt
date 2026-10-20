#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

struct CommitInfo
{
    QString sha;
    QStringList parents;
    QString authorName;
    QString authorEmail;
    QDateTime authorDate;
    QString subject;
};

struct RefInfo
{
    enum Kind { LocalBranch, RemoteBranch, Tag, Stash };

    QString name;
    QString fullName;
    QString sha; // commit the ref points to, tags peeled
    Kind kind = LocalBranch;
};

struct CommitStats
{
    int files = 0;
    int added = 0;
    int removed = 0; // binary files count as files only
};

// Arguments for `git log` that list the stats of the commits appended to
// them, in the format parseNumstat() expects.
QStringList numstatArgs();
QHash<QString, CommitStats> parseNumstat(const QByteArray &out);

// Arguments for `git log` that produce the format parseLog() expects.
QStringList logFormatArgs();
QList<CommitInfo> parseLog(const QByteArray &out);

// Arguments for `git for-each-ref` that produce the format parseRefs() expects.
QStringList refFormatArgs();
QList<RefInfo> parseRefs(const QByteArray &out);
