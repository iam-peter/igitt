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
