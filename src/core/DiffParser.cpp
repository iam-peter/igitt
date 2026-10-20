#include "DiffParser.h"

#include <QRegularExpression>

namespace {

QString stripPrefix(const QByteArray &path)
{
    QByteArray p = path;
    // git appends a tab to ---/+++ paths that contain spaces.
    if (p.endsWith('\t'))
        p.chop(1);
    if (p == "/dev/null")
        return {};
    if (p.startsWith("a/") || p.startsWith("b/"))
        p = p.mid(2);
    return QString::fromUtf8(p);
}

// "diff --git a/x b/x" is ambiguous when paths contain spaces; it is only
// needed for diffs without ---/+++ lines (binary files, pure mode changes),
// where old and new path are the same unless a rename line says otherwise.
void pathsFromDiffGitLine(const QByteArray &line, FileDiff &file)
{
    const QByteArray rest = line.mid(int(sizeof("diff --git ") - 1));
    const qsizetype half = rest.size() / 2;
    if (rest.size() % 2 == 1 && rest.at(half) == ' ') {
        const QByteArray a = rest.left(half);
        const QByteArray b = rest.mid(half + 1);
        if (a.mid(2) == b.mid(2)) {
            file.oldPath = stripPrefix(a);
            file.newPath = stripPrefix(b);
        }
    }
}

} // namespace

QByteArray DiffHunk::header() const
{
    QByteArray h = "@@ -" + QByteArray::number(oldStart) + ',' + QByteArray::number(oldCount)
                 + " +" + QByteArray::number(newStart) + ',' + QByteArray::number(newCount) + " @@";
    if (!section.isEmpty())
        h += ' ' + section;
    return h;
}

QList<FileDiff> parseDiff(const QByteArray &data)
{
    static const QRegularExpression hunkRe(
        QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@ ?(.*)$"));

    QList<FileDiff> files;
    FileDiff *file = nullptr;
    DiffHunk *hunk = nullptr;
    int oldLeft = 0;
    int newLeft = 0;
    int oldLine = 0;
    int newLine = 0;

    QList<QByteArray> lines = data.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    for (const QByteArray &line : std::as_const(lines)) {
        // Inside a hunk the line counts tell where it ends; this keeps a
        // removed line that reads "--- foo" from looking like a header.
        if (hunk && (oldLeft > 0 || newLeft > 0 || line.startsWith('\\'))) {
            DiffLine dl;
            dl.content = line.mid(1);
            const char marker = line.isEmpty() ? ' ' : line.at(0);
            switch (marker) {
            case '+':
                dl.kind = DiffLine::Added;
                dl.newLine = newLine++;
                --newLeft;
                break;
            case '-':
                dl.kind = DiffLine::Removed;
                dl.oldLine = oldLine++;
                --oldLeft;
                break;
            case '\\':
                dl.kind = DiffLine::NoNewline;
                break;
            default:
                dl.kind = DiffLine::Context;
                dl.oldLine = oldLine++;
                dl.newLine = newLine++;
                --oldLeft;
                --newLeft;
                break;
            }
            hunk->lines.append(dl);
            continue;
        }

        if (line.startsWith("diff --git ")) {
            files.append(FileDiff{});
            file = &files.last();
            hunk = nullptr;
            file->headerLines.append(line);
            pathsFromDiffGitLine(line, *file);
            continue;
        }
        if (!file)
            continue;

        if (line.startsWith("@@ ")) {
            const QRegularExpressionMatch m = hunkRe.match(QString::fromUtf8(line));
            if (!m.hasMatch())
                continue;
            file->hunks.append(DiffHunk{});
            hunk = &file->hunks.last();
            hunk->oldStart = m.captured(1).toInt();
            hunk->oldCount = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
            hunk->newStart = m.captured(3).toInt();
            hunk->newCount = m.captured(4).isEmpty() ? 1 : m.captured(4).toInt();
            hunk->section = m.captured(5).toUtf8();
            oldLeft = hunk->oldCount;
            newLeft = hunk->newCount;
            oldLine = hunk->oldStart;
            newLine = hunk->newStart;
            continue;
        }
        if (hunk)
            continue; // stray line after a hunk ended

        file->headerLines.append(line);
        if (line.startsWith("--- ")) {
            file->oldPath = stripPrefix(line.mid(4));
        } else if (line.startsWith("+++ ")) {
            file->newPath = stripPrefix(line.mid(4));
        } else if (line.startsWith("new file mode")) {
            file->isNew = true;
            file->oldPath.clear();
        } else if (line.startsWith("deleted file mode")) {
            file->isDeleted = true;
            file->newPath.clear();
        } else if (line.startsWith("rename from ")) {
            file->oldPath = QString::fromUtf8(line.mid(12));
        } else if (line.startsWith("rename to ")) {
            file->newPath = QString::fromUtf8(line.mid(10));
        } else if (line.startsWith("Binary files ") || line.startsWith("GIT binary patch")) {
            file->isBinary = true;
        }
    }
    return files;
}
