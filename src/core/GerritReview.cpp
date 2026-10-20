#include "GerritReview.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("GerritReview", text);
}

QString config(const GitRunner &git, const QString &key)
{
    const GitResult r = git.run({QStringLiteral("config"), QStringLiteral("--get"), key});
    return r.ok() ? QString::fromUtf8(r.out).trimmed() : QString();
}

} // namespace

GerritRemote parseGerritRemote(const QString &url)
{
    static const QRegularExpression re(QStringLiteral("^ssh://(?:([^@/]+)@)?([^:/]+)(?::(\\d+))?/(.+?)(?:\\.git)?/?$"));
    GerritRemote remote;
    const QRegularExpressionMatch m = re.match(url.trimmed());
    if (!m.hasMatch())
        return remote;
    remote.user = m.captured(1);
    remote.host = m.captured(2);
    if (!m.captured(3).isEmpty())
        remote.port = m.captured(3).toInt();
    remote.project = m.captured(4);
    return remote;
}

QHash<QString, GerritChange> parseGerritQuery(const QByteArray &output)
{
    QHash<QString, GerritChange> changes;
    for (const QByteArray &line : output.split('\n')) {
        const QJsonObject object = QJsonDocument::fromJson(line).object();
        if (object.isEmpty() || object.contains(QLatin1StringView("type")))
            continue; // the closing statistics line
        GerritChange change;
        change.changeId = object.value(QLatin1StringView("id")).toString();
        change.number = object.value(QLatin1StringView("number")).toVariant().toInt();
        change.url = object.value(QLatin1StringView("url")).toString();
        change.status = object.value(QLatin1StringView("status")).toString();
        const QJsonObject patchSet = object.value(QLatin1StringView("currentPatchSet")).toObject();
        change.revision = patchSet.value(QLatin1StringView("revision")).toString();
        change.patchSet = patchSet.value(QLatin1StringView("number")).toVariant().toInt();
        if (!change.changeId.isEmpty())
            changes.insert(change.changeId, change);
    }
    return changes;
}

ReviewQuery queryReviews(const GitRunner &git, const QString &remote, const QString &upstream,
                         const QString &branch)
{
    ReviewQuery query;
    const GerritRemote gerrit = parseGerritRemote(config(git, QStringLiteral("remote.%1.url").arg(remote)));
    if (!gerrit.isValid()) {
        query.error = tr("Only Gerrit remotes reached over SSH can be asked about reviews.");
        return query;
    }

    // The commits a push for review would send, with their Change-Ids.
    QHash<QString, QString> shaByChangeId;
    QStringList shas;
    const GitResult log = git.run({QStringLiteral("log"),
                                   QStringLiteral("--format=%H%x1f%(trailers:key=Change-Id,valueonly,separator=%x20)"),
                                   upstream + QStringLiteral("..HEAD"), QStringLiteral("--")});
    if (!log.ok()) {
        query.error = log.errorText();
        return query;
    }
    for (const QByteArray &line : log.out.split('\n')) {
        const QList<QByteArray> fields = line.split('\x1f');
        if (fields.size() < 2 || fields[0].isEmpty())
            continue;
        const QString sha = QString::fromLatin1(fields[0]);
        shas.append(sha);
        const QString changeId = QString::fromUtf8(fields[1]).trimmed().section(QLatin1Char(' '), -1);
        if (!changeId.isEmpty())
            shaByChangeId.insert(changeId, sha);
    }
    query.ok = true;
    for (const QString &sha : std::as_const(shas))
        query.bySha.insert(sha, ReviewInfo{});
    if (shaByChangeId.isEmpty())
        return query;

    // The same Change-Id lives on in cherry-picks to other branches, so ask
    // only about this project and branch.
    QStringList ids = shaByChangeId.keys();
    ids.sort();
    QStringList words{QStringLiteral("gerrit"), QStringLiteral("query"), QStringLiteral("--format=JSON"),
                      QStringLiteral("--current-patch-set"), QStringLiteral("project:") + gerrit.project,
                      QStringLiteral("branch:") + branch, QStringLiteral("(")};
    for (qsizetype i = 0; i < ids.size(); ++i) {
        if (i > 0)
            words.append(QStringLiteral("OR"));
        words.append(QStringLiteral("change:") + ids[i]);
    }
    words.append(QStringLiteral(")"));

    // ssh as git would run it, without prompts.
    QString sshCommand = QProcessEnvironment::systemEnvironment().value(QStringLiteral("GIT_SSH_COMMAND"));
    if (sshCommand.isEmpty())
        sshCommand = config(git, QStringLiteral("core.sshCommand"));
    QStringList command = QProcess::splitCommand(sshCommand.isEmpty() ? QStringLiteral("ssh") : sshCommand);
    const QString program = command.takeFirst();
    command += {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"), QStringLiteral("-o"),
                QStringLiteral("ConnectTimeout=15"), QStringLiteral("-p"), QString::number(gerrit.port),
                gerrit.user.isEmpty() ? gerrit.host : gerrit.user + QLatin1Char('@') + gerrit.host};

    QProcess ssh;
    ssh.setWorkingDirectory(git.workDir());
    ssh.start(program, command + words);
    if (!ssh.waitForStarted() || !ssh.waitForFinished(60000) || ssh.exitStatus() != QProcess::NormalExit
        || ssh.exitCode() != 0) {
        ssh.kill();
        query.ok = false;
        query.error = tr("Asking Gerrit failed: %1")
                          .arg(QString::fromUtf8(ssh.readAllStandardError()).trimmed()
                               + (ssh.error() == QProcess::UnknownError ? QString() : ssh.errorString()));
        return query;
    }

    const QHash<QString, GerritChange> changes = parseGerritQuery(ssh.readAllStandardOutput());
    for (auto it = shaByChangeId.cbegin(); it != shaByChangeId.cend(); ++it) {
        const auto change = changes.constFind(it.key());
        if (change == changes.cend())
            continue;
        ReviewInfo info;
        info.number = change->number;
        info.patchSet = change->patchSet;
        info.url = change->url;
        if (change->status == QLatin1StringView("MERGED") || change->status == QLatin1StringView("ABANDONED"))
            info.state = ReviewInfo::Closed;
        else
            info.state = change->revision == it.value() ? ReviewInfo::UpToDate : ReviewInfo::Outdated;
        query.bySha.insert(it.value(), info);
    }
    return query;
}
