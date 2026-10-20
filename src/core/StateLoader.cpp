#include "StateLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>

namespace {

constexpr int MaxHistory = 20000;
constexpr int MaxFoldTargets = 1000;
constexpr qsizetype MaxContentBytes = 8 * 1024 * 1024;

QString tr(const char *text)
{
    return QCoreApplication::translate("StateLoader", text);
}

QStringList lines(const QByteArray &out)
{
    return QString::fromLatin1(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out.left(out.indexOf('\n'))).trimmed();
}

} // namespace

QStringList diffFormatArgs()
{
    return {QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
            QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/"),
            QStringLiteral("--find-renames")};
}

StatusInfo loadSimulatedStatus(const StateSource &source)
{
    const GitRunner &git = source.git;
    StatusInfo info;
    // Entries the simulation wrote carry no stat data; without a refresh
    // they would all look modified.
    git.run({QStringLiteral("update-index"), QStringLiteral("-q"), QStringLiteral("--refresh")});

    const GitResult head = git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                    QStringLiteral("-q"), source.head + QStringLiteral("^{commit}")});
    info.headSha = head.ok() ? firstLine(head.out) : QString();
    info.branch = firstLine(git.run({QStringLiteral("symbolic-ref"), QStringLiteral("-q"),
                                     QStringLiteral("--short"), QStringLiteral("HEAD")}).out);
    const GitResult upstream = git.run({QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"),
                                        QStringLiteral("--symbolic-full-name"), QStringLiteral("@{upstream}")});
    if (upstream.ok() && !info.headSha.isEmpty()) {
        info.upstream = firstLine(upstream.out);
        const QStringList counts = firstLine(git.run({QStringLiteral("rev-list"), QStringLiteral("--left-right"),
                                                      QStringLiteral("--count"),
                                                      info.headSha + QStringLiteral("...@{upstream}")}).out)
                                       .split(QLatin1Char('\t'));
        info.ahead = counts.value(0).toInt();
        info.behind = counts.value(1).toInt();
    }

    QMap<QString, StatusEntry> entries; // sorted by path, as git status does
    const auto entry = [&entries](const QString &path) -> StatusEntry & {
        StatusEntry &e = entries[path];
        e.path = path;
        return e;
    };

    // Staged: index against the simulated HEAD.
    if (info.headSha.isEmpty()) {
        for (const QByteArray &path : git.run({QStringLiteral("ls-files"), QStringLiteral("-z")}).out.split('\0')) {
            if (!path.isEmpty())
                entry(QString::fromUtf8(path)).index = QLatin1Char('A');
        }
    } else {
        const QList<QByteArray> fields = git.run({QStringLiteral("diff-index"), QStringLiteral("--cached"),
                                                  QStringLiteral("-z"), QStringLiteral("-M"),
                                                  QStringLiteral("--name-status"), info.headSha}).out.split('\0');
        for (qsizetype i = 0; i + 1 < fields.size(); i += 2) {
            const QByteArray &status = fields[i];
            if (status.isEmpty())
                break;
            if (status.startsWith('R') || status.startsWith('C')) {
                if (i + 2 >= fields.size())
                    break;
                StatusEntry &e = entry(QString::fromUtf8(fields[i + 2]));
                e.index = QLatin1Char(status.at(0));
                e.origPath = QString::fromUtf8(fields[i + 1]);
                ++i;
            } else {
                entry(QString::fromUtf8(fields[i + 1])).index = QLatin1Char(status.at(0));
            }
        }
    }

    // Unstaged: working tree against the index.
    const QList<QByteArray> files = git.run({QStringLiteral("diff-files"), QStringLiteral("-z"),
                                             QStringLiteral("--name-status")}).out.split('\0');
    for (qsizetype i = 0; i + 1 < files.size(); i += 2) {
        if (files[i].isEmpty())
            break;
        entry(QString::fromUtf8(files[i + 1])).worktree = QLatin1Char(files[i].at(0));
    }
    const QList<QByteArray> untracked = git.run({QStringLiteral("ls-files"), QStringLiteral("-z"),
                                                 QStringLiteral("--others"),
                                                 QStringLiteral("--exclude-standard")}).out.split('\0');
    for (const QByteArray &path : untracked) {
        if (path.isEmpty())
            continue;
        StatusEntry &e = entry(QString::fromUtf8(path));
        e.untracked = true;
        e.worktree = QLatin1Char('?');
    }

    info.entries = entries.values();
    return info;
}

HistoryState loadHistory(const StateSource &source, const StatusInfo &status, const HistoryFilter &filter,
                         const PushSettings &push)
{
    const GitRunner &git = source.git;
    HistoryState history;
    history.pushTarget = resolvePushTarget(git, push);
    history.head = status.headSha;
    history.wip = !status.entries.isEmpty();
    history.refs = parseRefs(git.run(QStringList{QStringLiteral("for-each-ref")} + refFormatArgs()).out);
    history.refs.removeIf([&filter](const RefInfo &ref) {
        return (ref.kind == RefInfo::RemoteBranch && !filter.remotes) || (ref.kind == RefInfo::Tag && !filter.tags);
    });

    // In a simulation the checked out branch points at the simulated HEAD,
    // and its real commits must not show up as a second line of history.
    const QString branchRef = QStringLiteral("refs/heads/") + status.branch;
    if (source.simulated && !status.branch.isEmpty()) {
        for (qsizetype i = history.refs.size() - 1; i >= 0; --i) {
            if (history.refs[i].fullName != branchRef)
                continue;
            if (status.headSha.isEmpty())
                history.refs.removeAt(i);
            else
                history.refs[i].sha = status.headSha;
        }
    }
    if (history.refs.isEmpty() && status.headSha.isEmpty()) {
        if (history.wip)
            layoutGraphWithWip({}, {}, history.wipGraph);
        return history;
    }

    QStringList args{QStringLiteral("log"), QStringLiteral("--topo-order"),
                     QStringLiteral("--max-count=%1").arg(MaxHistory)};
    args += logFormatArgs();
    if (!filter.currentBranchOnly) {
        if (source.simulated && !status.branch.isEmpty())
            args.append(QStringLiteral("--exclude=") + branchRef);
        args.append(QStringLiteral("--branches"));
        if (filter.remotes)
            args.append(QStringLiteral("--remotes"));
        if (filter.tags)
            args.append(QStringLiteral("--tags"));
    } else if (status.headSha.isEmpty()) {
        return history;
    }
    if (!status.headSha.isEmpty())
        args.append(status.headSha);
    args.append(QStringLiteral("--"));
    const GitResult log = git.run(args);
    if (!log.ok()) {
        history.error = tr("Reading history failed: %1").arg(log.errorText());
        return history;
    }
    history.commits = parseLog(log.out);
    history.graph = history.wip ? layoutGraphWithWip(history.commits, status.headSha, history.wipGraph)
                                : layoutGraph(history.commits);

    if (status.headSha.isEmpty())
        return history;
    const QString head = status.headSha;
    history.headMessage = QString::fromUtf8(git.run({QStringLiteral("log"), QStringLiteral("-1"),
                                                     QStringLiteral("--format=%B"), head}).out).trimmed();

    // A fold can rewrite the first-parent chain down to the first merge,
    // the merge itself included (see FoldEngine::blocker()).
    const GitResult chain = git.run({QStringLiteral("rev-list"), QStringLiteral("--first-parent"),
                                     QStringLiteral("--parents"),
                                     QStringLiteral("--max-count=%1").arg(MaxFoldTargets), head});
    for (const QString &line : lines(chain.out)) {
        const QStringList ids = line.split(QLatin1Char(' '));
        history.foldTargets.append(ids.first());
        if (ids.size() > 2)
            break;
    }
    const GitResult unpublished = git.run({QStringLiteral("rev-list"),
                                           QStringLiteral("--max-count=%1").arg(MaxFoldTargets),
                                           head, QStringLiteral("--not"), QStringLiteral("--remotes")});
    for (const QString &sha : lines(unpublished.out))
        history.unpublished.insert(sha);

    if (source.simulated) {
        QStringList planned{QStringLiteral("rev-list"), QStringLiteral("--max-count=%1").arg(MaxFoldTargets), head};
        if (!source.baselineHead.isEmpty())
            planned << QStringLiteral("--not") << source.baselineHead;
        for (const QString &sha : lines(git.run(planned).out))
            history.planned.insert(sha);
    }
    return history;
}

DiffState loadDiff(const StateSource &source, const DiffRequest &request, const StatusInfo &status)
{
    const GitRunner &git = source.git;
    DiffState diff;
    diff.request = request;

    if (request.kind == DiffKind::Commit) {
        diff.details = QString::fromUtf8(git.run({
            QStringLiteral("show"), QStringLiteral("-s"), QStringLiteral("--date=iso"),
            QStringLiteral("--format=commit %H%nParents: %P%nAuthor: %an <%ae>  %ad%n"
                           "Committer: %cn <%ce>  %cd%n%n%B"),
            request.commit, QStringLiteral("--"),
        }).out).trimmed();
        const GitResult result = git.run(QStringList{QStringLiteral("show"), QStringLiteral("--format="),
                                                     QStringLiteral("--diff-merges=first-parent")}
                                         + diffFormatArgs() + QStringList{request.commit, QStringLiteral("--")});
        if (result.ok())
            diff.files = parseDiff(result.out);
        else
            diff.error = tr("Showing commit %1 failed: %2").arg(request.commit.left(10), result.errorText());
        return diff;
    }
    if (request.kind == DiffKind::Range) {
        const QString commit = QString::fromUtf8(git.run({QStringLiteral("show"), QStringLiteral("-s"),
                                                          QStringLiteral("--format=%h %s"), request.commit,
                                                          QStringLiteral("--")}).out).trimmed();
        diff.details = tr("From %1 (%2)\nto %3").arg(request.baseName, request.base.left(10), commit);
        const GitResult parents = git.run({QStringLiteral("rev-parse"), request.base + QStringLiteral("^@"),
                                           request.commit + QStringLiteral("^@")});
        const QList<QByteArray> ids = parents.out.trimmed().split('\n');
        if (parents.ok() && (ids.size() != 2 || ids[0] != ids[1]))
            diff.details += QLatin1String("\n\n")
                + tr("They are on different parents, so this includes what the rebase brought in.");
        const GitResult result = git.run(QStringList{QStringLiteral("diff")} + diffFormatArgs()
                                         + QStringList{request.base, request.commit, QStringLiteral("--")});
        if (result.ok())
            diff.files = parseDiff(result.out);
        else
            diff.error = tr("Comparing with %1 failed: %2").arg(request.baseName, result.errorText());
        if (result.ok() && diff.files.isEmpty())
            diff.message = tr("No differences.");
        return diff;
    }
    if (request.kind == DiffKind::Content) {
        // The file as it is on disk; if it is gone from there, as staged.
        QByteArray data;
        QFile file(QDir(git.workDir()).filePath(request.path));
        if (file.open(QIODevice::ReadOnly)) {
            data = file.read(MaxContentBytes + 1);
        } else {
            const GitResult staged = git.run({QStringLiteral("show"), QLatin1Char(':') + request.path});
            if (!staged.ok()) {
                diff.error = tr("Reading %1 failed: %2").arg(request.path, staged.errorText());
                return diff;
            }
            data = staged.out.left(MaxContentBytes + 1);
        }
        if (data.left(8000).contains('\0')) {
            diff.message = tr("Binary file");
            return diff;
        }
        diff.truncated = data.size() > MaxContentBytes;
        if (diff.truncated)
            data.truncate(data.lastIndexOf('\n', MaxContentBytes) + 1);
        if (data.endsWith('\n'))
            data.chop(1);
        diff.content = QString::fromUtf8(data).split(QLatin1Char('\n'));
        return diff;
    }
    if (request.kind != DiffKind::Unstaged && request.kind != DiffKind::Staged)
        return diff;

    const bool staged = request.kind == DiffKind::Staged;
    const StatusEntry *entry = nullptr;
    for (const StatusEntry &e : status.entries) {
        if (e.path == request.path && (staged ? e.hasStagedChange() : e.hasUnstagedChange()))
            entry = &e;
    }
    if (!entry)
        return diff; // staged, committed or reverted meanwhile
    if (entry->conflicted) {
        diff.conflicted = true;
        diff.message = tr("Unresolved merge conflict.");
        return diff;
    }

    QStringList args{QStringLiteral("diff")};
    if (entry->untracked)
        args.append(QStringLiteral("--no-index"));
    else if (staged)
        args.append(QStringLiteral("--cached"));
    args += diffFormatArgs();
    if (staged && !entry->untracked && !status.headSha.isEmpty())
        args.append(status.headSha);
    args.append(QStringLiteral("--"));
    if (entry->untracked)
        args.append(QStringLiteral("/dev/null"));
    else if (staged && !entry->origPath.isEmpty())
        args.append(entry->origPath);
    args.append(request.path);

    const GitResult result = git.run(args);
    // `git diff --no-index` exits with 1 when the files differ.
    if (!result.ok() && !(entry->untracked && result.exitCode == 1)) {
        diff.error = tr("Showing %1 failed: %2").arg(request.path, result.errorText());
        return diff;
    }
    diff.files = parseDiff(result.out);
    diff.suggester = FoldSuggester(git);
    diff.suggester.analyze(diff.files, staged ? FoldBase::Head : FoldBase::Index,
                           status.headSha.isEmpty() ? source.head : status.headSha);
    return diff;
}

RepositoryState loadState(const StateSource &source, const DiffRequest &diff, const HistoryRequest &history)
{
    RepositoryState state;
    if (source.simulated) {
        state.status = loadSimulatedStatus(source);
    } else {
        const GitResult result = source.git.run({QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                                                 QStringLiteral("-z"), QStringLiteral("--branch"),
                                                 QStringLiteral("--untracked-files=all")});
        if (!result.ok()) {
            state.error = tr("Reading status failed: %1").arg(result.errorText());
            return state;
        }
        state.status = parseStatus(result.out);
    }
    if (history.force || state.status->headSha != history.knownHead
        || state.status->entries.isEmpty() == history.knownWip)
        state.history = loadHistory(source, *state.status, history.filter, history.push);
    if (diff.kind == DiffKind::Unstaged || diff.kind == DiffKind::Staged)
        state.diff = loadDiff(source, diff, *state.status);
    return state;
}

QHash<QString, CommitStats> loadCommitStats(const GitRunner &git, const QStringList &shas)
{
    if (shas.isEmpty())
        return {};
    const GitResult result = git.run(numstatArgs() + shas + QStringList{QStringLiteral("--")});
    QHash<QString, CommitStats> stats = parseNumstat(result.out);
    // Commits without file changes print nothing; they still have stats.
    for (const QString &sha : shas)
        stats.try_emplace(sha);
    return stats;
}
