#include "ReviewProvider.h"

#include "GerritReview.h"
#include "GitHubReview.h"
#include "GitLabReview.h"

#include <QCoreApplication>
#include <QMap>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace {

QString tr(const char *text, const char *comment = nullptr, int n = -1)
{
    return QCoreApplication::translate("ReviewProvider", text, comment, n);
}

QString stripPath(QString path)
{
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (path.endsWith(QLatin1StringView(".git")))
        path.chop(4);
    return path;
}

bool listed(const QStringList &hosts, const QString &host)
{
    return std::any_of(hosts.cbegin(), hosts.cend(), [&host](const QString &entry) {
        const QString name = QUrl::fromUserInput(entry.trimmed()).host();
        return !name.isEmpty() && name.compare(host, Qt::CaseInsensitive) == 0;
    });
}

// Whether `host` is `name` itself or a server named after it, such as
// gitlab.example.org.
bool namedAfter(const QString &host, QLatin1StringView name)
{
    return host == name + QLatin1StringView(".com") || host.startsWith(name + QLatin1Char('.'));
}

} // namespace

RemoteUrl parseRemoteUrl(const QString &text)
{
    RemoteUrl remote;
    const QString url = text.trimmed();
    if (url.contains(QLatin1StringView("://"))) {
        const QUrl parsed(url);
        if (parsed.scheme() == QLatin1StringView("file"))
            return remote;
        remote.user = parsed.userName();
        remote.host = parsed.host();
        remote.port = parsed.port(0);
        remote.path = stripPath(parsed.path());
        return remote;
    }
    // The scp-like form; a host of one letter would be a Windows drive.
    static const QRegularExpression scp(QStringLiteral("^(?:([^@/]+)@)?([^:/]{2,}):(.+)$"));
    const QRegularExpressionMatch m = scp.match(url);
    if (m.hasMatch()) {
        remote.user = m.captured(1);
        remote.host = m.captured(2);
        remote.path = stripPath(m.captured(3));
    }
    return remote;
}

QString remoteUrl(const GitRunner &git, const QString &remote)
{
    const GitResult r = git.run({QStringLiteral("config"), QStringLiteral("--get"),
                                 QStringLiteral("remote.%1.url").arg(remote)});
    return r.ok() ? QString::fromUtf8(r.out).trimmed() : QString();
}

ReviewHost detectReviewHost(const GitRunner &git, const QString &remote, const PushSettings &settings)
{
    switch (settings.mode) {
    case PushMode::Plain: return ReviewHost::None;
    case PushMode::Gerrit: return ReviewHost::Gerrit;
    case PushMode::GitHub: return ReviewHost::GitHub;
    case PushMode::GitLab: return ReviewHost::GitLab;
    case PushMode::Auto: break;
    }
    if (remote.isEmpty())
        return ReviewHost::None;
    if (looksLikeGerrit(git, remote))
        return ReviewHost::Gerrit;
    const QString host = parseRemoteUrl(remoteUrl(git, remote)).host.toLower();
    if (host.isEmpty())
        return ReviewHost::None;
    if (listed(settings.githubHosts, host))
        return ReviewHost::GitHub;
    if (listed(settings.gitlabHosts, host))
        return ReviewHost::GitLab;
    // ssh.github.com serves SSH over port 443.
    if (namedAfter(host, QLatin1StringView("github")) || host == QLatin1StringView("ssh.github.com"))
        return ReviewHost::GitHub;
    if (namedAfter(host, QLatin1StringView("gitlab")))
        return ReviewHost::GitLab;
    return ReviewHost::None;
}

const ReviewProvider *reviewProvider(ReviewHost host)
{
    switch (host) {
    case ReviewHost::Gerrit: return &gerritProvider();
    case ReviewHost::GitHub: return &gitHubProvider();
    case ReviewHost::GitLab: return &gitLabProvider();
    case ReviewHost::None: break;
    }
    return nullptr;
}

bool runReviewTool(const GitRunner &git, const QString &program, const QStringList &args,
                   const QProcessEnvironment &env, const QString &host, const QString &missing,
                   QByteArray *out, QString *error)
{
    QProcess process;
    process.setWorkingDirectory(git.workDir());
    if (!env.isEmpty()) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(env);
        process.setProcessEnvironment(environment);
    }
    process.start(program, args);
    if (!process.waitForStarted()) {
        *error = process.error() == QProcess::FailedToStart
                     ? missing
                     : tr("Asking %1 failed: %2").arg(host, process.errorString());
        return false;
    }
    // Nothing to answer prompts with.
    process.closeWriteChannel();
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.kill();
        *error = tr("Asking %1 failed: %2")
                     .arg(host, QString::fromUtf8(process.readAllStandardError()).trimmed()
                                    + (process.error() == QProcess::UnknownError ? QString() : process.errorString()));
        return false;
    }
    *out = process.readAllStandardOutput();
    return true;
}

QString shellCommand(const QStringList &words)
{
    static const QRegularExpression plain(QStringLiteral("^[\\w@%+=:,./-]+$"));
    QStringList quoted;
    for (const QString &word : words) {
        if (plain.match(word).hasMatch())
            quoted << word;
        else
            quoted << QLatin1Char('\'') + QString(word).replace(QLatin1Char('\''), QStringLiteral("'\\''")) + QLatin1Char('\'');
    }
    return quoted.join(QLatin1Char(' '));
}

QString BranchReview::reference() const
{
    return (host == ReviewHost::GitLab ? QLatin1Char('!') : QLatin1Char('#')) + QString::number(number);
}

QString BranchReview::label() const
{
    return (host == ReviewHost::GitLab ? QStringLiteral("MR ") : QStringLiteral("PR ")) + reference();
}

namespace {

QStringList badgeList(const BranchReview &review)
{
    QStringList parts;
    if (review.state == BranchReview::State::Merged)
        parts << tr("merged");
    else if (review.state == BranchReview::State::Closed)
        parts << tr("closed");
    if (review.draft)
        parts << tr("draft");
    if (review.state != BranchReview::State::Open)
        return parts;
    switch (review.decision) {
    case BranchReview::Decision::Approved: parts << tr("approved"); break;
    case BranchReview::Decision::ChangesRequested: parts << tr("changes requested"); break;
    case BranchReview::Decision::ReviewRequired:
        parts << (review.approvalsLeft > 0 ? tr("%n approval(s) left", "", review.approvalsLeft)
                                           : tr("review required"));
        break;
    case BranchReview::Decision::None: break;
    }
    const bool gitLab = review.host == ReviewHost::GitLab;
    switch (review.checks) {
    case BranchReview::Checks::Failure: parts << (gitLab ? tr("pipeline failed") : tr("checks failed")); break;
    case BranchReview::Checks::Pending: parts << (gitLab ? tr("pipeline running") : tr("checks running")); break;
    case BranchReview::Checks::Success: parts << (gitLab ? tr("pipeline passed") : tr("checks passed")); break;
    case BranchReview::Checks::None: break;
    }
    if (review.conflicts)
        parts << tr("conflicts");
    if (review.unresolved.value_or(0) > 0)
        parts << tr("%n unresolved", "", *review.unresolved);
    return parts;
}

QString noun(const BranchReview &review)
{
    return review.host == ReviewHost::GitLab ? tr("Merge request") : tr("Pull request");
}

} // namespace

QString BranchReview::badges() const
{
    return badgeList(*this).join(QStringLiteral(" · "));
}

QString BranchReview::summary() const
{
    QStringList parts = badgeList(*this);
    if (state == State::Open)
        parts.prepend(tr("open"));
    return tr("%1 %2: %3.").arg(noun(*this), reference(), parts.join(QStringLiteral(", ")));
}

QString BranchReview::details(const QString &head) const
{
    QStringList lines;
    lines << QStringLiteral("%1 %2: %3").arg(noun(*this), reference(), title);
    QString state = this->state == State::Merged   ? tr("Merged")
                  : this->state == State::Closed ? tr("Closed")
                                                 : tr("Open");
    if (draft)
        state += tr(", draft");
    if (!target.isEmpty())
        state += tr(", into %1").arg(target);
    lines << state;
    if (!approvedBy.isEmpty())
        lines << tr("Approved by %1").arg(approvedBy.join(QStringLiteral(", ")));
    if (!changesRequestedBy.isEmpty())
        lines << tr("Changes requested by %1").arg(changesRequestedBy.join(QStringLiteral(", ")));
    if (approvalsLeft > 0)
        lines << tr("%n more approval(s) required", "", approvalsLeft);
    else if (decision == Decision::ReviewRequired)
        lines << tr("Review required");

    // Checks: "4 passed, 1 failed", then the ones that did not pass.
    QMap<QString, QStringList> byResult;
    for (const auto &[name, result] : checkResults)
        byResult[result].append(name);
    if (!byResult.isEmpty()) {
        QStringList counts;
        for (auto it = byResult.cbegin(); it != byResult.cend(); ++it)
            counts << QStringLiteral("%1 %2").arg(it->size()).arg(it.key());
        lines << (host == ReviewHost::GitLab ? tr("Pipeline: %1") : tr("Checks: %1"))
                     .arg(counts.join(QStringLiteral(", ")));
        for (auto it = byResult.cbegin(); it != byResult.cend(); ++it) {
            static const QStringList fine{QStringLiteral("passed"), QStringLiteral("skipped"),
                                          QStringLiteral("neutral")};
            if (fine.contains(it.key()) || host == ReviewHost::GitLab)
                continue;
            QStringList names = it->mid(0, 8);
            if (it->size() > names.size())
                names << QStringLiteral("…");
            lines << QStringLiteral("  %1: %2").arg(it.key(), names.join(QStringLiteral(", ")));
        }
    }
    if (conflicts)
        lines << tr("Conflicts with %1").arg(target.isEmpty() ? tr("the target branch") : target);
    if (unresolved.has_value())
        lines << (*unresolved > 0 ? tr("%n unresolved thread(s)", "", *unresolved) : tr("No unresolved threads"));
    if (this->state == State::Open && !headSha.isEmpty() && !head.isEmpty() && headSha != head)
        lines << tr("It has %1, not this commit; pushing updates it.").arg(headSha.left(8));
    return lines.join(QLatin1Char('\n'));
}
