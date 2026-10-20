#include "DiffMarkup.h"

#include <vector>

namespace {

constexpr int TabWidth = 4;

} // namespace

QString expandTabs(const QString &text)
{
    if (!text.contains(QLatin1Char('\t')))
        return text;
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\t'))
            out.append(QString(TabWidth - out.size() % TabWidth, QLatin1Char(' ')));
        else
            out.append(ch);
    }
    return out;
}

QList<FileMarkup> markupDiff(const QList<FileDiff> &files, const MarkupLimits &limits)
{
    QList<FileMarkup> result;
    result.reserve(files.size());
    int highlightLeft = limits.highlightLines;
    int wordDiffLeft = limits.wordDiffLines;

    for (const FileDiff &file : files) {
        FileMarkup markup;
        // Old and new side each read as a file of their own: a removed
        // line continues the old text, an added line the new.
        SyntaxHighlighter before(file.oldPath.isEmpty() ? file.newPath : file.oldPath);
        SyntaxHighlighter after(file.path());
        const bool highlight = !file.isBinary && (before.isValid() || after.isValid());

        for (const DiffHunk &hunk : file.hunks) {
            QList<LineMarkup> lines(hunk.lines.size());
            // Decoded when first needed: past the limits, lines are not.
            QStringList texts(hunk.lines.size());
            std::vector<bool> decoded(hunk.lines.size(), false);
            const auto text = [&](qsizetype l) -> const QString & {
                if (!decoded[l]) {
                    texts[l] = expandTabs(QString::fromUtf8(hunk.lines[l].content));
                    decoded[l] = true;
                }
                return texts[l];
            };

            // A hunk starts somewhere in the file; whatever was open before
            // it is unknown.
            before.reset();
            after.reset();
            for (qsizetype l = 0; highlight && l < hunk.lines.size() && highlightLeft > 0; ++l) {
                const DiffLine::Kind kind = hunk.lines[l].kind;
                if (kind == DiffLine::NoNewline)
                    continue;
                if (text(l).size() > limits.lineLength) {
                    // Too long to be code worth coloring; start over after it.
                    before.reset();
                    after.reset();
                    continue;
                }
                --highlightLeft;
                if (kind == DiffLine::Removed) {
                    lines[l].syntax = before.highlightLine(text(l));
                } else if (kind == DiffLine::Added) {
                    lines[l].syntax = after.highlightLine(text(l));
                } else {
                    before.highlightLine(text(l));
                    lines[l].syntax = after.highlightLine(text(l));
                }
            }

            for (const auto &[removed, added] : pairChangedLines(hunk.lines)) {
                if (wordDiffLeft <= 0)
                    break;
                if (text(removed).size() > limits.lineLength || text(added).size() > limits.lineLength)
                    continue;
                --wordDiffLeft;
                WordChanges changes = diffWords(text(removed), text(added));
                lines[removed].changes = std::move(changes.before);
                lines[added].changes = std::move(changes.after);
            }
            markup.hunks.append(lines);
        }
        result.append(markup);
    }
    return result;
}

QList<LineMarkup> markupContent(const QString &path, const QStringList &lines, const MarkupLimits &limits)
{
    SyntaxHighlighter highlighter(path);
    if (!highlighter.isValid())
        return {};
    const qsizetype count = std::min<qsizetype>(lines.size(), limits.highlightLines);
    QList<LineMarkup> result(count);
    for (qsizetype i = 0; i < count; ++i) {
        const QString text = expandTabs(lines[i]);
        if (text.size() > limits.lineLength) {
            highlighter.reset();
            continue;
        }
        result[i].syntax = highlighter.highlightLine(text);
    }
    return result;
}
