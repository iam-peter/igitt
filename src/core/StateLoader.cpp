#include "StateLoader.h"

#include <QCoreApplication>

namespace {

constexpr int MaxHistory = 20000;
constexpr int MaxFoldTargets = 1000;

QString tr(const char *text)
{
    return QCoreApplication::translate("StateLoader", text);
}

QStringList lines(const QByteArray &out)
{
    return QString::fromLatin1(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

} // namespace

QStringList diffFormatArgs()
{
    return {QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
            QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/"),
            QStringLiteral("--find-renames")};
}

HistoryState loadHistory(const GitRunner &git, const StatusInfo &status)
{
    HistoryState history;
    history.head = status.headSha;
    history.refs = parseRefs(git.run(QStringList{QStringLiteral("for-each-ref")} + refFormatArgs()).out);
    if (history.refs.isEmpty() && status.headSha.isEmpty())
        return history;

    QStringList args{QStringLiteral("log"), QStringLiteral("--topo-order"),
                     QStringLiteral("--max-count=%1").arg(MaxHistory)};
    args += logFormatArgs();
    args += {QStringLiteral("--branches"), QStringLiteral("--remotes"), QStringLiteral("--tags")};
    if (!status.headSha.isEmpty())
        args.append(QStringLiteral("HEAD"));
    args.append(QStringLiteral("--"));
    const GitResult log = git.run(args);
    if (!log.ok()) {
        history.error = tr("Reading history failed: %1").arg(log.errorText());
        return history;
    }
    history.commits = parseLog(log.out);
    history.graph = layoutGraph(history.commits);

    if (status.headSha.isEmpty())
        return history;
    history.headMessage = QString::fromUtf8(git.run({QStringLiteral("log"), QStringLiteral("-1"),
                                                     QStringLiteral("--format=%B")}).out).trimmed();

    // A fold can rewrite the first-parent chain down to the first merge,
    // the merge itself included (see FoldEngine::blocker()).
    const GitResult chain = git.run({QStringLiteral("rev-list"), QStringLiteral("--first-parent"),
                                     QStringLiteral("--parents"),
                                     QStringLiteral("--max-count=%1").arg(MaxFoldTargets), QStringLiteral("HEAD")});
    for (const QString &line : lines(chain.out)) {
        const QStringList ids = line.split(QLatin1Char(' '));
        history.foldTargets.append(ids.first());
        if (ids.size() > 2)
            break;
    }
    const GitResult unpublished = git.run({QStringLiteral("rev-list"),
                                           QStringLiteral("--max-count=%1").arg(MaxFoldTargets),
                                           QStringLiteral("HEAD"), QStringLiteral("--not"), QStringLiteral("--remotes")});
    for (const QString &sha : lines(unpublished.out))
        history.unpublished.insert(sha);
    return history;
}

DiffState loadDiff(const GitRunner &git, const DiffRequest &request, const StatusInfo &status)
{
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
        diff.message = tr("Unresolved merge conflict. Resolve it in your editor, then stage the file.");
        return diff;
    }

    QStringList args{QStringLiteral("diff")};
    if (entry->untracked)
        args.append(QStringLiteral("--no-index"));
    else if (staged)
        args.append(QStringLiteral("--cached"));
    args += diffFormatArgs();
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
    diff.suggester.analyze(diff.files, staged ? FoldBase::Head : FoldBase::Index);
    return diff;
}

RepositoryState loadState(const GitRunner &git, const DiffRequest &diff, const QString &knownHead,
                          bool forceHistory)
{
    RepositoryState state;
    const GitResult result = git.run({QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                                      QStringLiteral("-z"), QStringLiteral("--branch"),
                                      QStringLiteral("--untracked-files=all")});
    if (!result.ok()) {
        state.error = tr("Reading status failed: %1").arg(result.errorText());
        return state;
    }
    state.status = parseStatus(result.out);
    if (forceHistory || state.status->headSha != knownHead)
        state.history = loadHistory(git, *state.status);
    if (diff.kind == DiffKind::Unstaged || diff.kind == DiffKind::Staged)
        state.diff = loadDiff(git, diff, *state.status);
    return state;
}
