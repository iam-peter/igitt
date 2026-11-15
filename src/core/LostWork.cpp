#include "LostWork.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace {

constexpr char Separator = '\x1f';

// A stash's commit: a merge of the HEAD it was made on and its index,
// "WIP on main: ..." or "On main: <message>"
bool looksLikeStash(const QString &subject, int parents)
{
    static const QRegularExpression pattern(QStringLiteral("^(WIP on|On) [^:]+: "));
    return parents >= 2 && pattern.match(subject).hasMatch();
}

} // namespace

QList<LostCommit> findLostWork(const GitRunner &git, int reflogLimit)
{
    QList<LostCommit> found;

    // %gd HEAD@{n}, %gs what moved it
    const GitResult reflog = git.run({QStringLiteral("reflog"), QStringLiteral("show"),
                                      QStringLiteral("--format=%H%x1f%gd%x1f%gs%x1f%ct%x1f%P%x1f%s"),
                                      QStringLiteral("-n"), QString::number(reflogLimit), QStringLiteral("HEAD"),
                                      QStringLiteral("--")});
    if (reflog.ok()) {
        for (const QByteArray &line : reflog.out.split('\n')) {
            const QList<QByteArray> fields = line.split(Separator);
            if (fields.size() < 6)
                continue;
            LostCommit commit;
            commit.kind = LostCommit::Reflog;
            commit.sha = QString::fromLatin1(fields[0]);
            commit.selector = QString::fromUtf8(fields[1]);
            commit.action = QString::fromUtf8(fields[2]);
            commit.time = fields[3].toLongLong();
            commit.subject = QString::fromUtf8(fields.mid(5).join(Separator));
            found.append(commit);
        }
    }

    // Dangling: nothing refers to them, not even a reflog
    const GitResult fsck = git.run({QStringLiteral("fsck"), QStringLiteral("--dangling"),
                                    QStringLiteral("--no-progress")});
    QStringList dangling;
    for (const QByteArray &line : fsck.out.split('\n')) {
        if (line.startsWith("dangling commit "))
            dangling.append(QString::fromLatin1(line.mid(16).trimmed()));
    }
    if (!dangling.isEmpty()) {
        const GitResult log = git.run(QStringList{QStringLiteral("log"), QStringLiteral("--no-walk=unsorted"),
                                                  QStringLiteral("--format=%H%x1f%ct%x1f%P%x1f%s")}
                                      + dangling + QStringList{QStringLiteral("--")});
        QList<LostCommit> lost;
        for (const QByteArray &line : log.out.split('\n')) {
            const QList<QByteArray> fields = line.split(Separator);
            if (fields.size() < 4)
                continue;
            LostCommit commit;
            commit.kind = LostCommit::Dangling;
            commit.sha = QString::fromLatin1(fields[0]);
            commit.time = fields[1].toLongLong();
            commit.subject = QString::fromUtf8(fields.mid(3).join(Separator));
            commit.unreachable = true;
            commit.stash = looksLikeStash(commit.subject, int(fields[2].split(' ').size()));
            lost.append(commit);
        }
        std::stable_sort(lost.begin(), lost.end(),
                         [](const LostCommit &a, const LostCommit &b) { return a.time > b.time; });
        found += lost;
    }

    // Which of where HEAD was no ref has any more: what is reachable from
    // them but not from a branch, tag or stash
    QStringList moved;
    QSet<QString> seen;
    for (const LostCommit &commit : std::as_const(found)) {
        if (commit.kind == LostCommit::Reflog && !seen.contains(commit.sha)) {
            seen.insert(commit.sha);
            moved.append(commit.sha);
        }
    }
    if (!moved.isEmpty()) {
        const GitResult rest = git.run(QStringList{QStringLiteral("rev-list")} + moved
                                       + QStringList{QStringLiteral("--not"), QStringLiteral("--all"),
                                                     QStringLiteral("--")});
        QSet<QString> unreachable;
        for (const QByteArray &sha : rest.out.split('\n')) {
            if (!sha.isEmpty())
                unreachable.insert(QString::fromLatin1(sha));
        }
        for (LostCommit &commit : found) {
            if (commit.kind == LostCommit::Reflog)
                commit.unreachable = unreachable.contains(commit.sha);
        }
    }
    return found;
}
