#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

struct DiffLine
{
    enum Kind : char { Context = ' ', Added = '+', Removed = '-', NoNewline = '\\' };

    Kind kind = Context;
    QByteArray content; // without the leading marker, raw bytes as in the file
    int oldLine = -1;
    int newLine = -1;
};

struct DiffHunk
{
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    QByteArray section; // text after the closing "@@"
    QList<DiffLine> lines;

    QByteArray header() const;
};

struct FileDiff
{
    QString oldPath; // empty for an added file
    QString newPath; // empty for a deleted file
    QList<QByteArray> headerLines; // everything from "diff --git" to the first hunk
    bool isNew = false;
    bool isDeleted = false;
    bool isBinary = false;
    QList<DiffHunk> hunks;

    QString path() const { return newPath.isEmpty() ? oldPath : newPath; }
};

// A file kept in Git LFS changes only its pointer in git: the object's
// size before and after, -1 where there is none (added, deleted)
struct LfsChange
{
    qint64 oldSize = -1;
    qint64 newSize = -1;
};
// The change, if every line of the file's diff is an LFS pointer's
std::optional<LfsChange> lfsChange(const FileDiff &file);

// Parses the output of `git diff`/`git show` in the default unified format
// with the a/ and b/ prefixes. Combined diffs of merges are not supported;
// ask git for a first-parent diff instead.
QList<FileDiff> parseDiff(const QByteArray &data);
