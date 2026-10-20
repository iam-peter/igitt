#include "LogParser.h"

namespace {

constexpr char FieldSep = '\x1f';

} // namespace

QStringList logFormatArgs()
{
    return {QStringLiteral("-z"),
            QStringLiteral("--format=%H%x1f%P%x1f%an%x1f%ae%x1f%at%x1f%s")};
}

QList<CommitInfo> parseLog(const QByteArray &out)
{
    QList<CommitInfo> commits;
    const QList<QByteArray> records = out.split('\0');
    commits.reserve(records.size());
    for (const QByteArray &record : records) {
        // With -z, git separates entries by NUL; trim the newline it may
        // still put between them.
        const QByteArray rec = record.trimmed();
        if (rec.isEmpty())
            continue;
        const QList<QByteArray> fields = rec.split(FieldSep);
        if (fields.size() < 6)
            continue;

        CommitInfo c;
        c.sha = QString::fromLatin1(fields[0]);
        const QString parents = QString::fromLatin1(fields[1]);
        c.parents = parents.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        c.authorName = QString::fromUtf8(fields[2]);
        c.authorEmail = QString::fromUtf8(fields[3]);
        c.authorDate = QDateTime::fromSecsSinceEpoch(fields[4].toLongLong());
        c.subject = QString::fromUtf8(fields.mid(5).join(FieldSep));
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
