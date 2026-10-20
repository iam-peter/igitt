#pragma once

#include "GitRunner.h"

#include <QDateTime>
#include <QList>
#include <QString>

// One revision of a file, as `git log --follow` lists it.
struct FileRevision
{
    QString sha;
    QString path; // the file's name in this revision
    QString author;
    QDateTime date;
    QString subject;
};

// A line of a file at some revision, for the time-lapse view.
struct TimelapseLine
{
    enum Kind { Line, Deleted };

    Kind kind = Line;
    QString text;
    int lineNo = 0;      // Line only
    int revision = -1;   // index into the revisions the line was last changed in; -1 if unknown
    QString sha;         // that commit
    QString author;
    bool changedHere = false; // changed by the revision on display
};

// The revisions of `path` reachable from `head`, oldest first, following
// renames.
QList<FileRevision> loadFileRevisions(const GitRunner &git, const QString &path,
                                      const QString &head = QStringLiteral("HEAD"));

// The file at `revisions[index]`, each line blamed to the revision that
// last changed it. With `withDeleted`, the lines that revision removed
// are listed where they used to be.
QList<TimelapseLine> loadTimelapse(const GitRunner &git, const QList<FileRevision> &revisions, int index,
                                   bool withDeleted, QString *error = nullptr);
