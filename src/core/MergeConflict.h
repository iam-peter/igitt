#pragma once

#include "GitRunner.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

// A conflicted file as runs of lines both sides agree on and conflicts
// with each side's version, as `git merge-file --diff3` sees them.
struct ConflictChunk
{
    bool conflict = false;
    QStringList lines; // when both sides agree
    QStringList ours;
    QStringList base;
    QStringList theirs;
};

// What to keep of one conflict: either side, both, or (neither) the
// conflict markers, left for editing.
struct ConflictChoice
{
    bool ours = false;
    bool theirs = false;
    bool theirsFirst = false; // with both: theirs was picked first
};

struct ConflictFile
{
    QString path;
    QString operation;   // "Merging", "Rebasing", ...
    QString oursLabel;   // what the sides are, e.g. "HEAD: 1a2b3c4 Fix the thing"
    QString theirsLabel;
    QList<ConflictChunk> chunks;
    bool endsWithNewline = true;
    // Set when the conflict cannot be shown line by line: binary, or the
    // file was deleted on one side.
    QString error;
    bool deletedInOurs = false;
    bool deletedInTheirs = false;
};

// "Merging", "Rebasing", ... while git is stopped in such an operation;
// empty otherwise.
QString operationInProgress(const GitRunner &git);

QList<ConflictChunk> parseMergeOutput(const QByteArray &merged);
ConflictFile loadConflict(const GitRunner &git, const QString &path);
QString resolveConflict(const ConflictFile &file, const QList<ConflictChoice> &choices);
