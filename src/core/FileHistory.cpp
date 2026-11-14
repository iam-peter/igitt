#include "FileHistory.h"

#include "DiffParser.h"
#include "StateLoader.h"

#include <QHash>
#include <QMap>
#include <QRegularExpression>

QList<FileRevision> loadFileRevisions(const GitRunner &git, const QString &path, const QString &head)
{
    const GitResult result = git.run({QStringLiteral("log"), QStringLiteral("--follow"), QStringLiteral("--name-only"),
                                      QStringLiteral("--format=%x01%H%x1f%an%x1f%at%x1f%s"), head,
                                      QStringLiteral("--"), path});
    QList<FileRevision> revisions;
    if (!result.ok())
        return revisions;
    for (const QByteArray &record : result.out.split('\x01')) {
        const QList<QByteArray> lines = record.trimmed().split('\n');
        const QList<QByteArray> fields = lines.first().split('\x1f');
        if (fields.size() < 4)
            continue;
        FileRevision revision;
        revision.sha = QString::fromLatin1(fields[0]);
        revision.author = QString::fromUtf8(fields[1]);
        revision.date = QDateTime::fromSecsSinceEpoch(fields[2].toLongLong());
        revision.subject = QString::fromUtf8(fields.mid(3).join('\x1f'));
        revision.path = QString::fromUtf8(lines.last().trimmed());
        revisions.prepend(revision);
    }
    return revisions;
}

QList<FlowRun> flowRuns(const QList<DiffHunk> &hunks, int oldLines)
{
    QList<FlowRun> runs;
    int oldPos = 0;
    int newPos = 0;
    const auto keep = [&](int until) {
        if (until > oldPos) {
            runs.append({FlowRun::Kept, oldPos, until - oldPos, newPos, until - oldPos});
            newPos += until - oldPos;
            oldPos = until;
        }
    };
    for (const DiffHunk &hunk : hunks) {
        // Without context, an empty side's start is the line before it
        const int oldBegin = hunk.oldCount > 0 ? hunk.oldStart - 1 : hunk.oldStart;
        keep(oldBegin);
        if (hunk.oldCount > 0)
            runs.append({FlowRun::Removed, oldPos, hunk.oldCount, newPos, 0});
        if (hunk.newCount > 0)
            runs.append({FlowRun::Added, oldPos + hunk.oldCount, 0, newPos, hunk.newCount});
        oldPos += hunk.oldCount;
        newPos += hunk.newCount;
    }
    keep(oldLines);
    return runs;
}

namespace {

// The lines of a blob, and whether it looks binary
std::pair<int, bool> countLines(const QByteArray &content)
{
    if (content.contains('\0'))
        return {0, true};
    int lines = int(content.count('\n'));
    if (!content.isEmpty() && !content.endsWith('\n'))
        ++lines;
    return {lines, false};
}

} // namespace

QList<FlowStep> loadFileFlow(const GitRunner &git, const QList<FileRevision> &revisions)
{
    QList<FlowStep> steps;
    for (int i = 0; i < revisions.size(); ++i) {
        const FileRevision &revision = revisions[i];
        const QString blob = revision.sha + QLatin1Char(':') + revision.path;
        FlowStep step;
        if (i == 0) {
            const auto [lines, binary] = countLines(git.run({QStringLiteral("cat-file"), QStringLiteral("-p"), blob}).out);
            step.lines = lines;
            step.added = lines;
            step.binary = binary;
            if (lines > 0)
                step.runs.append({FlowRun::Added, 0, 0, 0, lines});
            steps.append(step);
            continue;
        }
        const FileRevision &previous = revisions[i - 1];
        const int before = steps.last().lines;
        const GitResult diff = git.run(QStringList{QStringLiteral("diff"), QStringLiteral("-U0")} + diffFormatArgs()
                                       + QStringList{previous.sha + QLatin1Char(':') + previous.path, blob});
        const QList<FileDiff> files = parseDiff(diff.out);
        if (!files.isEmpty() && files.first().isBinary) {
            // No lines to follow: the whole file is new
            const auto [lines, binary] = countLines(git.run({QStringLiteral("cat-file"), QStringLiteral("-p"), blob}).out);
            step.binary = true;
            step.lines = lines;
            step.removed = before;
            step.added = lines;
            if (before > 0)
                step.runs.append({FlowRun::Removed, 0, before, 0, 0});
            if (lines > 0)
                step.runs.append({FlowRun::Added, before, 0, 0, lines});
            steps.append(step);
            continue;
        }
        const QList<DiffHunk> hunks = files.isEmpty() ? QList<DiffHunk>() : files.first().hunks;
        for (const DiffHunk &hunk : hunks) {
            step.added += hunk.newCount;
            step.removed += hunk.oldCount;
        }
        step.lines = before + step.added - step.removed;
        step.runs = flowRuns(hunks, before);
        steps.append(step);
    }
    return steps;
}

QList<TimelapseLine> loadTimelapse(const GitRunner &git, const QList<FileRevision> &revisions, int index,
                                   bool withDeleted, QString *error)
{
    QList<TimelapseLine> lines;
    if (index < 0 || index >= revisions.size())
        return lines;
    const FileRevision &revision = revisions[index];

    QHash<QString, int> indexBySha;
    for (int i = 0; i < revisions.size(); ++i)
        indexBySha.insert(revisions[i].sha, i);

    const GitResult blame = git.run({QStringLiteral("blame"), QStringLiteral("--porcelain"), revision.sha,
                                     QStringLiteral("--"), revision.path});
    if (!blame.ok()) {
        if (error)
            *error = blame.errorText();
        return lines;
    }

    // Porcelain: "<sha> <orig> <final> [<count>]", headers the first time a
    // commit appears, then the line itself after a tab.
    static const QRegularExpression header(QStringLiteral("^([0-9a-f]{40,64}) \\d+ (\\d+)"));
    QHash<QString, QString> authors;
    QString sha;
    int lineNo = 0;
    for (const QByteArray &raw : blame.out.split('\n')) {
        if (raw.startsWith('\t')) {
            TimelapseLine line;
            line.text = QString::fromUtf8(raw.mid(1));
            line.lineNo = lineNo;
            line.sha = sha;
            line.revision = indexBySha.value(sha, -1);
            line.author = authors.value(sha);
            line.changedHere = sha == revision.sha;
            lines.append(line);
            continue;
        }
        if (raw.startsWith("author ")) {
            authors.insert(sha, QString::fromUtf8(raw.mid(7)));
            continue;
        }
        const QRegularExpressionMatch m = header.match(QString::fromLatin1(raw));
        if (m.hasMatch()) {
            sha = m.captured(1);
            lineNo = m.captured(2).toInt();
        }
    }

    if (!withDeleted || index == 0)
        return lines;

    // What this revision removed, placed before the line that now follows.
    const FileRevision &previous = revisions[index - 1];
    const GitResult diff = git.run(QStringList{QStringLiteral("diff"), QStringLiteral("-U0")} + diffFormatArgs()
                                   + QStringList{previous.sha + QLatin1Char(':') + previous.path,
                                                 revision.sha + QLatin1Char(':') + revision.path});
    QMap<int, QStringList> deletedBefore;
    for (const FileDiff &file : parseDiff(diff.out)) {
        for (const DiffHunk &hunk : file.hunks) {
            const int position = hunk.newCount == 0 ? hunk.newStart + 1 : hunk.newStart;
            for (const DiffLine &line : hunk.lines) {
                if (line.kind == DiffLine::Removed)
                    deletedBefore[position].append(QString::fromUtf8(line.content));
            }
        }
    }
    if (deletedBefore.isEmpty())
        return lines;

    QList<TimelapseLine> merged;
    merged.reserve(lines.size() + deletedBefore.size());
    const auto addDeleted = [&](int position) {
        for (const QString &text : deletedBefore.value(position)) {
            TimelapseLine line;
            line.kind = TimelapseLine::Deleted;
            line.text = text;
            line.revision = index;
            line.sha = revision.sha;
            line.author = revision.author;
            line.changedHere = true;
            merged.append(line);
        }
    };
    for (const TimelapseLine &line : std::as_const(lines)) {
        addDeleted(line.lineNo);
        merged.append(line);
    }
    addDeleted(int(lines.size()) + 1);
    return merged;
}
