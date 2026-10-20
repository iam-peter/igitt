#include "Repository.h"

#include "core/DiffParser.h"
#include "core/LogParser.h"
#include "core/PatchBuilder.h"

#include <QUrl>

namespace {

constexpr int MaxHistory = 20000;

} // namespace

Repository::Repository(QObject *parent)
    : QObject(parent)
{
}

bool Repository::open(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;

    const GitResult result = GitRunner(path).run({QStringLiteral("rev-parse"),
                                                  QStringLiteral("--show-toplevel")});
    if (!result.ok()) {
        emit errorOccurred(tr("%1 is not inside a git work tree: %2").arg(path, result.errorText()));
        return false;
    }

    m_git.setWorkDir(QString::fromUtf8(result.out).trimmed());
    m_historyHead.clear();
    m_diffSource = NoDiff;
    m_diffPath.clear();
    m_diffCommit.clear();
    m_commitDetails.clear();
    m_diff.clear();
    emit pathChanged();
    emit diffChanged();

    refresh();
    return true;
}

void Repository::refresh()
{
    if (!valid())
        return;
    loadStatus();
    loadHistory();
    reloadDiff();
}

void Repository::refreshStatus()
{
    if (!valid())
        return;
    loadStatus();
    if (m_status.headSha != m_historyHead)
        loadHistory();
    reloadDiff();
}

void Repository::showCommit(const QString &sha)
{
    if (sha.isEmpty())
        return;
    m_diffSource = CommitDiff;
    m_diffPath.clear();
    m_diffCommit = sha;

    const GitResult details = m_git.run({
        QStringLiteral("show"), QStringLiteral("-s"), QStringLiteral("--date=iso"),
        QStringLiteral("--format=commit %H%nParents: %P%nAuthor: %an <%ae>  %ad%n"
                       "Committer: %cn <%ce>  %cd%n%n%B"),
        sha, QStringLiteral("--"),
    });
    m_commitDetails = QString::fromUtf8(details.out).trimmed();

    const GitResult diff = m_git.run(QStringList{QStringLiteral("show"), QStringLiteral("--format="),
                                                 QStringLiteral("--diff-merges=first-parent")}
                                     + diffArgs() + QStringList{sha, QStringLiteral("--")});
    if (check(diff, tr("Showing commit %1").arg(sha.left(10))))
        m_diff.setFiles(parseDiff(diff.out));
    else
        m_diff.clear();
    emit diffChanged();
}

void Repository::showFile(const QString &path, bool staged)
{
    m_diffSource = staged ? StagedDiff : UnstagedDiff;
    m_diffPath = path;
    m_diffCommit.clear();
    m_commitDetails.clear();
    reloadDiff();
    emit diffChanged();
}

bool Repository::stageFile(const QString &path)
{
    if (!check(m_git.run({QStringLiteral("add"), QStringLiteral("--"), path}),
               tr("Staging %1").arg(path)))
        return false;
    if (m_diffSource == UnstagedDiff && m_diffPath == path) {
        m_diffSource = StagedDiff;
        emit diffChanged();
    }
    refreshStatus();
    return true;
}

bool Repository::unstageFile(const QString &path)
{
    QStringList paths{path};
    if (const StatusEntry *entry = m_staged.entry(path); entry && !entry->origPath.isEmpty())
        paths.append(entry->origPath);

    const QStringList command = m_status.headSha.isEmpty()
        ? QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-q")}
        : QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD")};
    if (!check(m_git.run(command + QStringList{QStringLiteral("--")} + paths),
               tr("Unstaging %1").arg(path)))
        return false;
    if (m_diffSource == StagedDiff && m_diffPath == path) {
        m_diffSource = UnstagedDiff;
        emit diffChanged();
    }
    refreshStatus();
    return true;
}

bool Repository::stageAll()
{
    const bool ok = check(m_git.run({QStringLiteral("add"), QStringLiteral("-A")}), tr("Staging all changes"));
    refreshStatus();
    return ok;
}

bool Repository::unstageAll()
{
    const QStringList command = m_status.headSha.isEmpty()
        ? QStringList{QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"),
                      QStringLiteral("-q"), QStringLiteral(".")}
        : QStringList{QStringLiteral("reset"), QStringLiteral("-q")};
    const bool ok = check(m_git.run(command), tr("Unstaging all changes"));
    refreshStatus();
    return ok;
}

bool Repository::applySelection()
{
    if (m_diffSource != UnstagedDiff && m_diffSource != StagedDiff)
        return false;

    const bool unstage = m_diffSource == StagedDiff;
    const PatchDirection direction = unstage ? PatchDirection::Reverse : PatchDirection::Forward;
    QByteArray patch;
    for (int f = 0; f < m_diff.files().size(); ++f)
        patch += buildPatch(m_diff.files()[f], m_diff.selection(f), direction);
    if (patch.isEmpty())
        return false;

    QStringList args{QStringLiteral("apply"), QStringLiteral("--cached"),
                     QStringLiteral("--whitespace=nowarn")};
    if (unstage)
        args.append(QStringLiteral("--reverse"));
    const bool ok = check(m_git.run(args, patch),
                          unstage ? tr("Unstaging lines") : tr("Staging lines"));
    refreshStatus();
    return ok;
}

bool Repository::commit(const QString &message, bool amend)
{
    QStringList args{QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-")};
    if (amend)
        args.append(QStringLiteral("--amend"));
    const bool ok = check(m_git.run(args, message.toUtf8()), tr("Committing"));
    refresh();
    return ok;
}

QString Repository::headMessage() const
{
    const GitResult result = m_git.run({QStringLiteral("log"), QStringLiteral("-1"),
                                        QStringLiteral("--format=%B")});
    return QString::fromUtf8(result.out).trimmed();
}

bool Repository::check(const GitResult &result, const QString &what)
{
    if (result.ok())
        return true;
    emit errorOccurred(what + QStringLiteral(" failed: ") + result.errorText());
    return false;
}

void Repository::loadStatus()
{
    const GitResult result = m_git.run({QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                                        QStringLiteral("-z"), QStringLiteral("--branch"),
                                        QStringLiteral("--untracked-files=all")});
    if (!check(result, tr("Reading status")))
        return;
    m_status = parseStatus(result.out);
    m_unstaged.setEntries(m_status.entries);
    m_staged.setEntries(m_status.entries);
    emit headChanged();
}

void Repository::loadHistory()
{
    const GitResult refsResult = m_git.run(QStringList{QStringLiteral("for-each-ref")} + refFormatArgs());
    const QList<RefInfo> refs = parseRefs(refsResult.out);

    QList<CommitInfo> commits;
    if (!refs.isEmpty() || !m_status.headSha.isEmpty()) {
        QStringList args{QStringLiteral("log"), QStringLiteral("--topo-order"),
                         QStringLiteral("--max-count=%1").arg(MaxHistory)};
        args += logFormatArgs();
        args += {QStringLiteral("--branches"), QStringLiteral("--remotes"), QStringLiteral("--tags")};
        if (!m_status.headSha.isEmpty())
            args.append(QStringLiteral("HEAD"));
        args.append(QStringLiteral("--"));
        const GitResult result = m_git.run(args);
        if (check(result, tr("Reading history")))
            commits = parseLog(result.out);
    }
    m_commits.setCommits(commits, refs, m_status.headSha, m_status.branch);
    m_historyHead = m_status.headSha;
}

void Repository::reloadDiff()
{
    if (m_diffSource != UnstagedDiff && m_diffSource != StagedDiff)
        return;

    const bool staged = m_diffSource == StagedDiff;
    const StatusEntry *entry = (staged ? m_staged : m_unstaged).entry(m_diffPath);
    if (!entry) {
        m_diff.clear();
        return;
    }
    if (entry->conflicted) {
        m_diff.setMessage(tr("Unresolved merge conflict. Resolve it in your editor, then stage the file."));
        return;
    }

    QStringList args{QStringLiteral("diff")};
    if (entry->untracked)
        args.append(QStringLiteral("--no-index"));
    else if (staged)
        args.append(QStringLiteral("--cached"));
    args += diffArgs();
    args.append(QStringLiteral("--"));
    if (entry->untracked)
        args.append(QStringLiteral("/dev/null"));
    else if (staged && !entry->origPath.isEmpty())
        args.append(entry->origPath);
    args.append(m_diffPath);

    const GitResult result = m_git.run(args);
    // `git diff --no-index` exits with 1 when the files differ.
    if (result.ok() || (entry->untracked && result.exitCode == 1))
        m_diff.setFiles(parseDiff(result.out));
    else
        check(result, tr("Showing %1").arg(m_diffPath));
}

QStringList Repository::diffArgs() const
{
    // Pin everything the parser and `git apply` depend on, whatever the
    // user's diff configuration says.
    return {QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
            QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/"),
            QStringLiteral("--find-renames")};
}
