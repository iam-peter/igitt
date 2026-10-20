#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

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

// Parses the output of `git diff`/`git show` in the default unified format
// with the a/ and b/ prefixes. Combined diffs of merges are not supported;
// ask git for a first-parent diff instead.
QList<FileDiff> parseDiff(const QByteArray &data);
