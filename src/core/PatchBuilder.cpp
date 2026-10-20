#include "PatchBuilder.h"

namespace {

bool isSelected(const LineSelection &selection, qsizetype hunk, qsizetype line)
{
    return hunk < selection.size() && line < selection[hunk].size() && selection[hunk][line];
}

bool coversAllChanges(const FileDiff &file, const LineSelection &selection)
{
    for (qsizetype h = 0; h < file.hunks.size(); ++h) {
        const QList<DiffLine> &lines = file.hunks[h].lines;
        for (qsizetype l = 0; l < lines.size(); ++l) {
            const DiffLine::Kind kind = lines[l].kind;
            if ((kind == DiffLine::Added || kind == DiffLine::Removed) && !isSelected(selection, h, l))
                return false;
        }
    }
    return true;
}

// Applying part of a deletion leaves the file in place, and so does
// reverting part of a creation; the header must then describe a plain
// modification. The opposite cases still create or delete the file, only
// with fewer lines.
QList<QByteArray> headerFor(const FileDiff &file, bool partial, PatchDirection direction)
{
    const bool keepsFile = direction == PatchDirection::Forward ? file.isDeleted : file.isNew;
    if (!partial || !keepsFile)
        return file.headerLines;

    const QByteArray path = file.path().toUtf8();
    QList<QByteArray> header;
    for (const QByteArray &line : file.headerLines) {
        if (line.startsWith("new file mode") || line.startsWith("deleted file mode"))
            continue;
        if (line.startsWith("--- "))
            header.append("--- a/" + path);
        else if (line.startsWith("+++ "))
            header.append("+++ b/" + path);
        else
            header.append(line);
    }
    return header;
}

// In unified diffs a hunk side with zero lines names the line *before* the
// change; otherwise it names the first line of the hunk.
int lineBefore(int start, int count) { return count == 0 ? start : start - 1; }
int startFor(int before, int count) { return count == 0 ? before : before + 1; }

} // namespace

QByteArray buildPatch(const FileDiff &file, const LineSelection &selection,
                      PatchDirection direction)
{
    if (file.isBinary)
        return {};

    const bool forward = direction == PatchDirection::Forward;
    QByteArray body;
    int delta = 0; // new minus old line count of the hunks emitted so far

    for (qsizetype h = 0; h < file.hunks.size(); ++h) {
        const DiffHunk &hunk = file.hunks[h];
        QByteArray lines;
        int oldCount = 0;
        int newCount = 0;
        bool hasChange = false;
        bool lastDropped = false;

        for (qsizetype l = 0; l < hunk.lines.size(); ++l) {
            const DiffLine &line = hunk.lines[l];
            const bool selected = isSelected(selection, h, l);
            // What happens to an unselected change depends on which side of
            // the patch has to match the target: the old side going forward,
            // the new side in reverse.
            const bool keepAsContext =
                !selected && ((line.kind == DiffLine::Removed && forward)
                              || (line.kind == DiffLine::Added && !forward));
            switch (line.kind) {
            case DiffLine::Context:
                lines += ' ' + line.content + '\n';
                ++oldCount;
                ++newCount;
                lastDropped = false;
                break;
            case DiffLine::Added:
            case DiffLine::Removed:
                if (selected) {
                    lines += char(line.kind) + line.content + '\n';
                    line.kind == DiffLine::Added ? ++newCount : ++oldCount;
                    hasChange = true;
                    lastDropped = false;
                } else if (keepAsContext) {
                    lines += ' ' + line.content + '\n';
                    ++oldCount;
                    ++newCount;
                    lastDropped = false;
                } else {
                    lastDropped = true;
                }
                break;
            case DiffLine::NoNewline:
                if (!lastDropped)
                    lines += '\\' + line.content + '\n';
                break;
            }
        }
        if (!hasChange)
            continue;

        DiffHunk out;
        out.oldCount = oldCount;
        out.newCount = newCount;
        out.section = hunk.section;
        if (forward) {
            out.oldStart = startFor(lineBefore(hunk.oldStart, hunk.oldCount), oldCount);
            out.newStart = startFor(lineBefore(hunk.oldStart, hunk.oldCount) + delta, newCount);
        } else {
            out.newStart = startFor(lineBefore(hunk.newStart, hunk.newCount), newCount);
            out.oldStart = startFor(lineBefore(hunk.newStart, hunk.newCount) - delta, oldCount);
        }
        delta += newCount - oldCount;
        body += out.header() + '\n' + lines;
    }

    if (body.isEmpty())
        return {};

    QByteArray patch;
    const bool partial = !coversAllChanges(file, selection);
    for (const QByteArray &line : headerFor(file, partial, direction))
        patch += line + '\n';
    return patch + body;
}

LineSelection selectAll(const FileDiff &file)
{
    LineSelection selection;
    for (const DiffHunk &hunk : file.hunks)
        selection.append(QList<bool>(hunk.lines.size(), true));
    return selection;
}
