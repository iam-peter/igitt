#include "StatusParser.h"

namespace {

// Splits off the first `fields` space-separated fields; the rest of the
// record is the path, which may itself contain spaces.
QList<QByteArray> splitFields(const QByteArray &record, int fields)
{
    QList<QByteArray> parts;
    qsizetype pos = 0;
    for (int i = 0; i < fields; ++i) {
        const qsizetype space = record.indexOf(' ', pos);
        if (space < 0)
            return {};
        parts.append(record.mid(pos, space - pos));
        pos = space + 1;
    }
    parts.append(record.mid(pos));
    return parts;
}

void parseHeader(const QByteArray &record, StatusInfo &info)
{
    const QList<QByteArray> parts = splitFields(record, 2);
    if (parts.size() != 3)
        return;
    const QByteArray &key = parts[1];
    const QString value = QString::fromUtf8(parts[2]);
    if (key == "branch.oid") {
        info.headSha = value == QLatin1StringView("(initial)") ? QString() : value;
    } else if (key == "branch.head") {
        info.branch = value == QLatin1StringView("(detached)") ? QString() : value;
    } else if (key == "branch.upstream") {
        info.upstream = value;
    } else if (key == "branch.ab") {
        const QStringList ab = value.split(QLatin1Char(' '));
        if (ab.size() == 2) {
            info.ahead = ab[0].mid(1).toInt();
            info.behind = ab[1].mid(1).toInt();
        }
    }
}

} // namespace

StatusInfo parseStatus(const QByteArray &out)
{
    StatusInfo info;
    const QList<QByteArray> records = out.split('\0');
    for (qsizetype i = 0; i < records.size(); ++i) {
        const QByteArray &record = records[i];
        if (record.isEmpty())
            continue;

        StatusEntry entry;
        switch (record.at(0)) {
        case '#':
            parseHeader(record, info);
            continue;
        case '1': {
            const QList<QByteArray> f = splitFields(record, 8);
            if (f.size() != 9)
                continue;
            entry.index = QLatin1Char(f[1].at(0));
            entry.worktree = QLatin1Char(f[1].at(1));
            entry.path = QString::fromUtf8(f[8]);
            break;
        }
        case '2': {
            const QList<QByteArray> f = splitFields(record, 9);
            if (f.size() != 10)
                continue;
            entry.index = QLatin1Char(f[1].at(0));
            entry.worktree = QLatin1Char(f[1].at(1));
            entry.path = QString::fromUtf8(f[9]);
            // The original path follows as a separate NUL-terminated record.
            if (i + 1 < records.size())
                entry.origPath = QString::fromUtf8(records[++i]);
            break;
        }
        case 'u': {
            const QList<QByteArray> f = splitFields(record, 10);
            if (f.size() != 11)
                continue;
            entry.index = QLatin1Char(f[1].at(0));
            entry.worktree = QLatin1Char(f[1].at(1));
            entry.path = QString::fromUtf8(f[10]);
            entry.conflicted = true;
            break;
        }
        case '?':
            entry.path = QString::fromUtf8(record.mid(2));
            entry.untracked = true;
            entry.worktree = QLatin1Char('?');
            break;
        default:
            continue;
        }
        info.entries.append(entry);
    }
    return info;
}
