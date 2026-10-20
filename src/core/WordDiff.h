#pragma once

#include "DiffParser.h"

#include <QList>
#include <QStringView>

#include <utility>

struct TextRange
{
    int start = 0;
    int length = 0;

    friend bool operator==(const TextRange &, const TextRange &) = default;
};

struct WordChanges
{
    QList<TextRange> before; // in the removed line
    QList<TextRange> after;  // in the added line
};

// The words that differ between two versions of a line. Both lists stay
// empty when the lines have too little in common for marking words to
// help, or are too long to compare.
WordChanges diffWords(QStringView before, QStringView after);

// The removed and added lines of a hunk to compare word by word, as
// indices into `lines`: in each run of removed lines followed directly by
// added lines, the first removed with the first added, and so on.
QList<std::pair<int, int>> pairChangedLines(const QList<DiffLine> &lines);
