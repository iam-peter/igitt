#include "LogParser.h"

#include <QHash>
#include <QTimeZone>

namespace {

constexpr char FieldSep = '\x1f';

} // namespace

QStringList numstatArgs()
{
    return {QStringLiteral("log"), QStringLiteral("--no-walk=unsorted"),
            QStringLiteral("--diff-merges=first-parent"), QStringLiteral("--numstat"),
            QStringLiteral("--format=%x01%H")};
}

QHash<QString, CommitStats> parseNumstat(const QByteArray &out)
{
    QHash<QString, CommitStats> stats;
    CommitStats *current = nullptr;
    for (const QByteArray &line : out.split('\n')) {
        if (line.startsWith('\x01')) {
            current = &stats[QString::fromLatin1(line.mid(1).trimmed())];
            continue;
        }
        const QList<QByteArray> fields = line.split('\t');
        if (!current || fields.size() < 3)
            continue;
        ++current->files;
        current->added += fields[0].toInt(); // "-" for binary files
        current->removed += fields[1].toInt();
    }
    return stats;
}

QStringList logFormatArgs()
{
    return {QStringLiteral("-z"),
            QStringLiteral("--format=%H%x1f%P%x1f%an%x1f%ae%x1f%at%x1f%s")};
}

QList<CommitInfo> parseLog(const QByteArray &out)
{
    // Walked in place: no copy of a record or a field before it is a
    // string, and each author's name and address made once and shared
    QList<CommitInfo> commits;
    commits.reserve(out.count('\0') + 1);
    QHash<QByteArray, QString> names;
    const auto shared = [&names](QByteArrayView bytes) {
        auto it = names.constFind(bytes.toByteArray());
        if (it == names.cend())
            it = names.insert(bytes.toByteArray(), QString::fromUtf8(bytes));
        return it.value();
    };
    const QByteArrayView all(out);
    qsizetype from = 0;
    while (from < all.size()) {
        qsizetype end = all.indexOf('\0', from);
        if (end < 0)
            end = all.size();
        // With -z, git separates entries by NUL; trim the newline it may
        // still put between them.
        const QByteArrayView rec = all.sliced(from, end - from).trimmed();
        from = end + 1;
        if (rec.isEmpty())
            continue;
        QByteArrayView fields[5];
        qsizetype at = 0;
        int count = 0;
        for (; count < 5; ++count) {
            const qsizetype sep = rec.indexOf(FieldSep, at);
            if (sep < 0)
                break;
            fields[count] = rec.sliced(at, sep - at);
            at = sep + 1;
        }
        if (count < 5)
            continue;
        CommitInfo c;
        c.sha = QString::fromLatin1(fields[0]);
        for (qsizetype p = 0; p < fields[1].size();) {
            qsizetype space = fields[1].indexOf(' ', p);
            if (space < 0)
                space = fields[1].size();
            if (space > p)
                c.parents.append(QString::fromLatin1(fields[1].sliced(p, space - p)));
            p = space + 1;
        }
        c.authorName = shared(fields[2]);
        c.authorEmail = shared(fields[3]);
        // In UTC, which takes no time zone to work out; shown in local time
        c.authorDate = QDateTime::fromSecsSinceEpoch(fields[4].toLongLong(), QTimeZone::UTC);
        c.subject = QString::fromUtf8(rec.sliced(at)); // the rest, separators in it included
        commits.append(c);
    }
    return commits;
}

QStringList refFormatArgs()
{
    return {QStringLiteral("--format=%(objectname)%09%(*objectname)%09%(refname)"),
            QStringLiteral("refs/heads"), QStringLiteral("refs/remotes"),
            QStringLiteral("refs/tags"), QStringLiteral("refs/stash")};
}

QList<RefInfo> parseRefs(const QByteArray &out)
{
    QList<RefInfo> refs;
    const QList<QByteArray> lines = out.split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() != 3)
            continue;

        RefInfo ref;
        ref.fullName = QString::fromUtf8(fields[2]);
        // Annotated tags point at a tag object; the graph needs the commit.
        ref.sha = QString::fromLatin1(fields[1].isEmpty() ? fields[0] : fields[1]);

        static const struct { const char *prefix; RefInfo::Kind kind; } prefixes[] = {
            {"refs/heads/", RefInfo::LocalBranch},
            {"refs/remotes/", RefInfo::RemoteBranch},
            {"refs/tags/", RefInfo::Tag},
        };
        bool known = false;
        for (const auto &p : prefixes) {
            const QLatin1StringView prefix(p.prefix);
            if (ref.fullName.startsWith(prefix)) {
                ref.name = ref.fullName.mid(prefix.size());
                ref.kind = p.kind;
                known = true;
                break;
            }
        }
        if (!known && ref.fullName == QLatin1StringView("refs/stash")) {
            ref.name = QStringLiteral("stash");
            ref.kind = RefInfo::Stash;
            known = true;
        }
        // refs/remotes/origin/HEAD is a symref that only duplicates a label.
        if (!known || (ref.kind == RefInfo::RemoteBranch && ref.name.endsWith(QLatin1StringView("/HEAD"))))
            continue;
        refs.append(ref);
    }
    return refs;
}
