#include "RemoteOps.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("RemoteOps", text);
}

QString config(const GitRunner &git, const QString &key)
{
    const GitResult r = git.run({QStringLiteral("config"), QStringLiteral("--get"), key});
    return r.ok() ? QString::fromUtf8(r.out).trimmed() : QString();
}

QString firstLine(const QByteArray &out)
{
    return QString::fromUtf8(out.left(out.indexOf('\n'))).trimmed();
}

QString currentBranch(const GitRunner &git)
{
    return firstLine(git.run({QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("--short"),
                              QStringLiteral("HEAD")}).out);
}

// Without a terminal, prompts would hang or fail obscurely; make SSH fail
// fast instead, unless the user configured their own SSH command.
QProcessEnvironment networkEnvironment(const GitRunner &git)
{
    QProcessEnvironment env;
    const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();
    if (!system.contains(QStringLiteral("GIT_SSH_COMMAND")) && !system.contains(QStringLiteral("GIT_SSH"))
        && config(git, QStringLiteral("core.sshCommand")).isEmpty()) {
        env.insert(QStringLiteral("GIT_SSH_COMMAND"),
                   QStringLiteral("ssh -o BatchMode=yes -o ConnectTimeout=20"));
    }
    return env;
}

} // namespace

QString PushTarget::description() const
{
    if (!error.isEmpty())
        return error;
    if (gerrit)
        return tr("Push HEAD for review to refs/for/%1 on %2").arg(remoteBranch, remote);
    if (setsUpstream)
        return tr("Push to %1/%2 and track it").arg(remote, remoteBranch);
    return tr("Push to %1/%2").arg(remote, remoteBranch);
}

bool looksLikeGerrit(const GitRunner &git, const QString &remote)
{
    const QString pushSpecs = QString::fromUtf8(git.run({QStringLiteral("config"), QStringLiteral("--get-all"),
                                                         QStringLiteral("remote.%1.push").arg(remote)}).out);
    if (pushSpecs.contains(QLatin1StringView("refs/for/")))
        return true;
    if (config(git, QStringLiteral("remote.%1.url").arg(remote)).contains(QLatin1StringView(":29418")))
        return true;
    // --git-path honors core.hooksPath.
    const QString hook = firstLine(git.run({QStringLiteral("rev-parse"), QStringLiteral("--git-path"),
                                            QStringLiteral("hooks/commit-msg")}).out);
    QFile file(QDir(git.workDir()).filePath(hook));
    return file.open(QIODevice::ReadOnly) && file.readAll().contains("Change-Id");
}

QString upstreamRemote(const GitRunner &git)
{
    const QString branch = currentBranch(git);
    return branch.isEmpty() ? QString() : config(git, QStringLiteral("branch.%1.remote").arg(branch));
}

PushTarget resolvePushTarget(const GitRunner &git, const PushSettings &settings)
{
    PushTarget target;
    const QStringList remotes = QString::fromUtf8(git.run({QStringLiteral("remote")}).out)
                                    .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (remotes.isEmpty()) {
        target.error = tr("No remote is configured.");
        return target;
    }

    const QString branch = currentBranch(git);
    QString upstreamBranch;
    if (!branch.isEmpty()) {
        target.remote = config(git, QStringLiteral("branch.%1.remote").arg(branch));
        upstreamBranch = config(git, QStringLiteral("branch.%1.merge").arg(branch));
        upstreamBranch.remove(QRegularExpression(QStringLiteral("^refs/heads/")));
        if (target.remote.isEmpty())
            target.remote = config(git, QStringLiteral("branch.%1.pushRemote").arg(branch));
    }
    if (target.remote.isEmpty() || target.remote == QLatin1StringView("."))
        target.remote = config(git, QStringLiteral("remote.pushDefault"));
    if (target.remote.isEmpty())
        target.remote = remotes.contains(QLatin1StringView("origin")) ? QStringLiteral("origin") : remotes.first();

    target.gerrit = settings.mode == PushMode::Gerrit
                 || (settings.mode == PushMode::Auto && looksLikeGerrit(git, target.remote));
    if (target.gerrit) {
        target.remoteBranch = !settings.gerritBranch.isEmpty() ? settings.gerritBranch : upstreamBranch;
        if (target.remoteBranch.isEmpty()) {
            target.error = tr("No branch to push for review to: set an upstream, or the Gerrit branch in Settings.");
            return target;
        }
        target.args = {target.remote, QStringLiteral("HEAD:refs/for/") + target.remoteBranch};
        return target;
    }

    if (branch.isEmpty()) {
        target.error = tr("HEAD is detached; check out a branch to push it.");
        return target;
    }
    if (!upstreamBranch.isEmpty()) {
        target.remoteBranch = upstreamBranch;
        target.args = {target.remote, QStringLiteral("HEAD:refs/heads/") + upstreamBranch};
    } else {
        target.remoteBranch = branch;
        target.setsUpstream = true;
        target.args = {QStringLiteral("--set-upstream"), target.remote, branch};
    }
    return target;
}

NetworkResult pushTo(const GitRunner &git, const PushTarget &target, bool force)
{
    NetworkResult result;
    if (!target.error.isEmpty()) {
        result.message = target.error;
        return result;
    }
    QStringList args{QStringLiteral("push"), QStringLiteral("--porcelain")};
    if (force && !target.gerrit) {
        const QString tracking = QStringLiteral("refs/remotes/%1/%2").arg(target.remote, target.remoteBranch);
        const GitResult lease = git.run({QStringLiteral("rev-parse"), QStringLiteral("-q"),
                                         QStringLiteral("--verify"), tracking});
        args.append(lease.ok() ? QStringLiteral("--force-with-lease=refs/heads/%1:%2")
                                     .arg(target.remoteBranch, firstLine(lease.out))
                               : QStringLiteral("--force-with-lease"));
    }
    const GitResult r = git.run(args + target.args, {}, networkEnvironment(git));
    const QString err = QString::fromUtf8(r.err);
    result.links = remoteLinks(err);

    // --porcelain: "!" marks a rejected ref, followed by the reason.
    for (const QByteArray &line : r.out.split('\n')) {
        if (line.startsWith('!')) {
            result.rejected = true;
            const QList<QByteArray> fields = line.split('\t');
            result.message = QString::fromUtf8(fields.value(2)).trimmed();
        }
    }
    result.ok = r.ok() && !result.rejected;
    if (!result.ok && !result.rejected)
        result.message = explainNetworkFailure(err.isEmpty() ? r.errorText() : err);
    return result;
}

NetworkResult fetchFrom(const GitRunner &git, const QString &remote)
{
    NetworkResult result;
    const GitResult r = git.run({QStringLiteral("fetch"), QStringLiteral("--prune"),
                                 remote.isEmpty() ? QStringLiteral("--all") : remote},
                                {}, networkEnvironment(git));
    result.ok = r.ok();
    if (!result.ok)
        result.message = explainNetworkFailure(r.errorText());
    return result;
}

QStringList remoteLinks(const QString &stderrText)
{
    static const QRegularExpression url(QStringLiteral("^remote:\\s+(https?://\\S+)"),
                                        QRegularExpression::MultilineOption);
    QStringList links;
    for (auto it = url.globalMatch(stderrText); it.hasNext();)
        links.append(it.next().captured(1));
    return links;
}

QString explainNetworkFailure(const QString &stderrText)
{
    const QString text = stderrText.trimmed();
    if (text.contains(QLatin1StringView("terminal prompts disabled"))
        || text.contains(QLatin1StringView("could not read Username"))
        || text.contains(QLatin1StringView("could not read Password"))) {
        return tr("The remote asks for a user name and password, which igitt cannot prompt for. "
                  "Set up a credential helper (git config credential.helper) or use SSH.");
    }
    if (text.contains(QLatin1StringView("Host key verification failed")))
        return tr("The remote's SSH host key is not known yet. Connect once from a terminal to accept it.");
    if (text.contains(QLatin1StringView("Permission denied (publickey")))
        return tr("SSH authentication failed. Add your key to ssh-agent; igitt cannot ask for its passphrase.");
    return text;
}
