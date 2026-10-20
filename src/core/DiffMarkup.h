#pragma once

#include "DiffParser.h"
#include "Highlighter.h"
#include "WordDiff.h"

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

// How to draw one line of a diff or a file beyond its plain text: syntax
// colors, and for a changed line paired with its counterpart the words
// that changed. Offsets are into the line as shown, with tabs expanded.
struct LineMarkup
{
    QList<StyleSpan> syntax;
    QList<TextRange> changes;

    bool isEmpty() const { return syntax.isEmpty() && changes.isEmpty(); }
};

// Parallel to FileDiff::hunks and their lines.
struct FileMarkup
{
    QList<QList<LineMarkup>> hunks;
};

// Highlighting costs time and memory per line; big diffs and files only
// get it up to these limits.
struct MarkupLimits
{
    int highlightLines = 30000; // in all files together
    int wordDiffLines = 5000;   // pairs compared
    int lineLength = 2000;      // longer lines get neither
};

// Tabs expanded to the next multiple of four columns, as the view shows them.
QString expandTabs(const QString &text);

// Runs on the git thread, so that the UI thread only has to copy results.
QList<FileMarkup> markupDiff(const QList<FileDiff> &files, const MarkupLimits &limits = {});
QList<LineMarkup> markupContent(const QString &path, const QStringList &lines, const MarkupLimits &limits = {});

Q_DECLARE_METATYPE(LineMarkup)
