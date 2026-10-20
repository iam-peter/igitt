#include "MergeConflict.h"

#include <QFile>
#include <QHash>
#include <QTemporaryDir>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("MergeConflict", text);
}

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out.left(out.indexOf('\n'))).trimmed();
}

QStringList markers(const ConflictChunk &chunk)
{
    return QStringList{QStringLiteral("<<<<<<< ours")} + chunk.ours + QStringList{QStringLiteral("=======")}
         + chunk.theirs + QStringList{QStringLiteral(">>>>>>> theirs")};
}


const struct
{
    const char *ref;
    const char *operation;
} Operations[] = {
    {"MERGE_HEAD", QT_TRANSLATE_NOOP("MergeConflict", "Merging")},
    {"REBASE_HEAD", QT_TRANSLATE_NOOP("MergeConflict", "Rebasing")},
    {"CHERRY_PICK_HEAD", QT_TRANSLATE_NOOP("MergeConflict", "Cherry-picking")},
    {"REVERT_HEAD", QT_TRANSLATE_NOOP("MergeConflict", "Reverting")},
};

QString operationRef(const GitRunner &git, QString *operation)
{
    for (const auto &op : Operations) {
        const QString ref = QString::fromLatin1(op.ref);
        if (git.run({QStringLiteral("rev-parse"), QStringLiteral("-q"), QStringLiteral("--verify"), ref}).ok()) {
            if (operation)
                *operation = tr(op.operation);
            return ref;
        }
    }
    return {};
}

} // namespace

QString operationInProgress(const GitRunner &git)
{
    QString operation;
    operationRef(git, &operation);
    return operation;
}

QList<ConflictChunk> parseMergeOutput(const QByteArray &merged)
{
    enum State { Common, Ours, Base, Theirs } state = Common;
    QList<ConflictChunk> chunks;
    ConflictChunk current;

    QList<QByteArray> lines = merged.split('\n');
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    for (const QByteArray &raw : std::as_const(lines)) {
        const QString line = QString::fromUtf8(raw);
        if (state == Common && raw.startsWith("<<<<<<< ")) {
            if (!current.lines.isEmpty())
                chunks.append(current);
            current = ConflictChunk{};
            current.conflict = true;
            state = Ours;
        } else if (state == Ours && raw.startsWith("||||||| ")) {
            state = Base;
        } else if ((state == Ours || state == Base) && raw == "=======") {
            state = Theirs;
        } else if (state == Theirs && raw.startsWith(">>>>>>> ")) {
            chunks.append(current);
            current = ConflictChunk{};
            state = Common;
        } else {
            switch (state) {
            case Common: current.lines.append(line); break;
            case Ours: current.ours.append(line); break;
            case Base: current.base.append(line); break;
            case Theirs: current.theirs.append(line); break;
            }
        }
    }
    if (!current.lines.isEmpty() || current.conflict)
        chunks.append(current);
    return chunks;
}

ConflictFile loadConflict(const GitRunner &git, const QString &path)
{
    ConflictFile file;
    file.path = path;

    // Which operation stopped, and what the two sides are.
    const auto describe = [&git](const QString &ref) {
        return firstLine(git.run({QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%h %s"),
                                  ref, QStringLiteral("--")}).out);
    };
    file.oursLabel = tr("Ours: %1").arg(describe(QStringLiteral("HEAD")));
    const QString theirs = operationRef(git, &file.operation);
    file.theirsLabel = theirs.isEmpty() ? tr("Theirs") : tr("Theirs: %1").arg(describe(theirs));

    // Stage 1 is the common base, 2 ours, 3 theirs.
    QHash<int, QString> blobs;
    for (const QByteArray &entry : git.run({QStringLiteral("ls-files"), QStringLiteral("-u"), QStringLiteral("-z"),
                                            QStringLiteral("--"), path}).out.split('\0')) {
        const QList<QByteArray> fields = entry.left(entry.indexOf('\t')).split(' ');
        if (fields.size() == 3)
            blobs.insert(fields[2].toInt(), QString::fromLatin1(fields[1]));
    }
    if (blobs.isEmpty()) {
        file.error = tr("%1 is not in conflict.").arg(path);
        return file;
    }
    file.deletedInOurs = !blobs.contains(2);
    file.deletedInTheirs = !blobs.contains(3);
    if (file.deletedInOurs || file.deletedInTheirs) {
        file.error = file.deletedInOurs ? tr("Deleted on our side and changed on theirs.")
                                        : tr("Changed on our side and deleted on theirs.");
        return file;
    }

    QTemporaryDir dir;
    QByteArray contents[3];
    const QString names[3] = {QStringLiteral("ours"), QStringLiteral("base"), QStringLiteral("theirs")};
    const int stages[3] = {2, 1, 3};
    for (int i = 0; i < 3; ++i) {
        if (blobs.contains(stages[i]))
            contents[i] = git.run({QStringLiteral("cat-file"), QStringLiteral("blob"), blobs[stages[i]]}).out;
        if (contents[i].contains('\0')) {
            file.error = tr("Binary file; pick one side as a whole.");
            return file;
        }
        QFile out(dir.filePath(names[i]));
        if (!out.open(QIODevice::WriteOnly)) {
            file.error = tr("Could not write a temporary file.");
            return file;
        }
        out.write(contents[i]);
    }

    // Exits with the number of conflicts; only the output matters here.
    const GitResult merged = git.run({QStringLiteral("merge-file"), QStringLiteral("-p"), QStringLiteral("--diff3"),
                                      QStringLiteral("-L"), QStringLiteral("ours"), QStringLiteral("-L"),
                                      QStringLiteral("base"), QStringLiteral("-L"), QStringLiteral("theirs"),
                                      dir.filePath(names[0]), dir.filePath(names[1]), dir.filePath(names[2])});
    if (merged.exitCode < 0) {
        file.error = merged.errorText();
        return file;
    }
    file.chunks = parseMergeOutput(merged.out);
    file.endsWithNewline = merged.out.isEmpty() || merged.out.endsWith('\n');
    return file;
}

QString resolveConflict(const ConflictFile &file, const QList<ConflictChoice> &choices)
{
    QStringList lines;
    int conflict = 0;
    for (const ConflictChunk &chunk : file.chunks) {
        if (!chunk.conflict) {
            lines += chunk.lines;
            continue;
        }
        const ConflictChoice choice = choices.value(conflict++);
        if (choice.ours && choice.theirs)
            lines += choice.theirsFirst ? chunk.theirs + chunk.ours : chunk.ours + chunk.theirs;
        else if (choice.ours)
            lines += chunk.ours;
        else if (choice.theirs)
            lines += chunk.theirs;
        else
            lines += markers(chunk);
    }
    QString text = lines.join(QLatin1Char('\n'));
    if (file.endsWithNewline && !lines.isEmpty())
        text += QLatin1Char('\n');
    return text;
}
