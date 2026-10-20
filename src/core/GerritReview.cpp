#include "GerritReview.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>

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

QString signedVote(int value)
{
    return value > 0 ? QStringLiteral("+%1").arg(value) : QString::number(value);
}

// Runs `gerrit <words>` on `gerrit` with ssh as git would run it, without
// prompts; false and `error` if that failed.
bool runGerrit(const GitRunner &git, const GerritRemote &gerrit, const QStringList &words, QByteArray *out,
               QString *error)
{
    QString sshCommand = QProcessEnvironment::systemEnvironment().value(QStringLiteral("GIT_SSH_COMMAND"));
    if (sshCommand.isEmpty())
        sshCommand = config(git, QStringLiteral("core.sshCommand"));
    QStringList command = QProcess::splitCommand(sshCommand.isEmpty() ? QStringLiteral("ssh") : sshCommand);
    const QString program = command.takeFirst();
    command += {QStringLiteral("-o"), QStringLiteral("BatchMode=yes"), QStringLiteral("-o"),
                QStringLiteral("ConnectTimeout=15"), QStringLiteral("-p"), QString::number(gerrit.port),
                gerrit.user.isEmpty() ? gerrit.host : gerrit.user + QLatin1Char('@') + gerrit.host,
                QStringLiteral("gerrit")};
    return runReviewTool(git, program, command + words, {}, QStringLiteral("Gerrit"),
                         tr("Asking Gerrit needs ssh, which is not installed."), out, error);
}

QString topicKey(const QString &branch)
{
    return QStringLiteral("branch.%1.igittTopic").arg(branch);
}

// `query` scoped to the project and branch and limited to `changeIds`.
QStringList queryWords(const GerritRemote &gerrit, const QString &branch, QStringList changeIds,
                       const QStringList &options)
{
    changeIds.sort();
    QStringList words{QStringLiteral("query"), QStringLiteral("--format=JSON")};
    words += options;
    words += {QStringLiteral("project:") + gerrit.project, QStringLiteral("branch:") + branch, QStringLiteral("(")};
    for (qsizetype i = 0; i < changeIds.size(); ++i) {
        if (i > 0)
            words.append(QStringLiteral("OR"));
        words.append(QStringLiteral("change:") + changeIds[i]);
    }
    words.append(QStringLiteral(")"));
    return words;
}

} // namespace

std::pair<int, int> ReviewInfo::voteRange(const QString &label) const
{
    std::pair<int, int> range{0, 0};
    for (const GerritVote &vote : votes) {
        if (vote.label == label) {
            range.first = std::min(range.first, vote.value);
            range.second = std::max(range.second, vote.value);
        }
    }
    return range;
}

int ReviewInfo::shownVote(const QString &label) const
{
    const auto [min, max] = voteRange(label);
    return min < 0 ? min : max;
}

QString ReviewInfo::badges() const
{
    QStringList parts;
    if (wip)
        parts << tr("WIP");
    const std::pair<const char *, const char *> labels[] = {{"Code-Review", "CR"}, {"Verified", "V"}};
    for (const auto &[label, shortName] : labels) {
        if (const int vote = shownVote(QLatin1StringView(label)))
            parts << QLatin1StringView(shortName) + signedVote(vote);
    }
    if (unresolved.value_or(false))
        parts << tr("unresolved");
    return parts.join(QStringLiteral(" · "));
}

QString ReviewInfo::details() const
{
    QStringList lines;
    if (wip)
        lines << tr("Work in progress");
    if (!topic.isEmpty())
        lines << tr("Topic: %1").arg(topic);
    if (!hashtags.isEmpty())
        lines << tr("Hashtags: %1").arg(hashtags.join(QStringLiteral(", ")));

    // Code-Review and Verified first, the others as Gerrit sorts them.
    QStringList labels{QStringLiteral("Code-Review"), QStringLiteral("Verified")};
    for (const GerritVote &vote : votes) {
        if (!labels.contains(vote.label))
            labels.append(vote.label);
    }
    for (const QString &label : std::as_const(labels)) {
        QList<GerritVote> cast;
        for (const GerritVote &vote : votes) {
            if (vote.label == label && vote.value != 0)
                cast.append(vote);
        }
        if (cast.isEmpty())
            continue;
        std::stable_sort(cast.begin(), cast.end(), [](const GerritVote &a, const GerritVote &b) {
            return a.value > b.value;
        });
        QStringList who;
        for (const GerritVote &vote : std::as_const(cast))
            who << QStringLiteral("%1 %2").arg(signedVote(vote.value), vote.by);
        const auto [min, max] = voteRange(label);
        const QString range = min < 0 && max > 0 ? signedVote(min) + QStringLiteral(" / ") + signedVote(max)
                                                 : signedVote(min < 0 ? min : max);
        lines << QStringLiteral("%1 %2: %3").arg(label, range, who.join(QStringLiteral(", ")));
    }

    if (unresolved.has_value())
        lines << (*unresolved ? tr("Has unresolved comments") : tr("No unresolved comments"));
    if (comments > 0)
        lines << tr("Inline comments on patch set %1: %2").arg(patchSet).arg(comments);
    return lines.join(QLatin1Char('\n'));
}

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
        change.wip = object.value(QLatin1StringView("wip")).toBool();
        change.topic = object.value(QLatin1StringView("topic")).toString();
        for (const QJsonValue &tag : object.value(QLatin1StringView("hashtags")).toArray())
            change.hashtags.append(tag.toString());
        for (const QJsonValue &value : patchSet.value(QLatin1StringView("approvals")).toArray()) {
            const QJsonObject approval = value.toObject();
            const QJsonObject by = approval.value(QLatin1StringView("by")).toObject();
            GerritVote vote;
            vote.label = approval.value(QLatin1StringView("type")).toString();
            // A string such as "-1" or "2".
            vote.value = approval.value(QLatin1StringView("value")).toVariant().toInt();
            for (const char *key : {"name", "username", "email"}) {
                if (vote.by.isEmpty())
                    vote.by = by.value(QLatin1StringView(key)).toString();
            }
            if (!vote.label.isEmpty())
                change.votes.append(vote);
        }
        change.comments = int(patchSet.value(QLatin1StringView("comments")).toArray().size());
        if (!change.changeId.isEmpty())
            changes.insert(change.changeId, change);
    }
    return changes;
}

ReviewQuery queryGerritReviews(const GitRunner &git, const QString &remote, const QString &upstream,
                               const QString &branch)
{
    ReviewQuery query;
    query.host = ReviewHost::Gerrit;
    query.branch = branch;
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
    QByteArray out;
    if (!runGerrit(git, gerrit,
                   queryWords(gerrit, branch, shaByChangeId.keys(),
                              {QStringLiteral("--current-patch-set"), QStringLiteral("--comments")}),
                   &out, &query.error)) {
        query.ok = false;
        return query;
    }
    const QHash<QString, GerritChange> changes = parseGerritQuery(out);
    for (auto it = shaByChangeId.cbegin(); it != shaByChangeId.cend(); ++it) {
        const auto change = changes.constFind(it.key());
        if (change == changes.cend())
            continue;
        ReviewInfo info;
        info.number = change->number;
        info.patchSet = change->patchSet;
        info.url = change->url;
        info.wip = change->wip;
        info.topic = change->topic;
        info.hashtags = change->hashtags;
        info.votes = change->votes;
        info.comments = change->comments;
        if (change->status == QLatin1StringView("MERGED") || change->status == QLatin1StringView("ABANDONED"))
            info.state = ReviewInfo::Closed;
        else
            info.state = change->revision == it.value() ? ReviewInfo::UpToDate : ReviewInfo::Outdated;
        query.bySha.insert(it.value(), info);
    }

    // The JSON has no word on comment threads, but the search operator
    // does: a second round trip for the changes still open.
    QStringList open;
    for (auto it = shaByChangeId.cbegin(); it != shaByChangeId.cend(); ++it) {
        if (query.bySha.value(it.value()).number > 0 && query.bySha.value(it.value()).state != ReviewInfo::Closed)
            open.append(it.key());
    }
    QString unresolvedError;
    if (open.isEmpty()
        || !runGerrit(git, gerrit, queryWords(gerrit, branch, open, {}) << QStringLiteral("has:unresolved"),
                      &out, &unresolvedError)) {
        return query;
    }
    const QHash<QString, GerritChange> unresolved = parseGerritQuery(out);
    for (const QString &changeId : std::as_const(open))
        query.bySha[shaByChangeId.value(changeId)].unresolved = unresolved.contains(changeId);
    return query;
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
    const GitResult hook = git.run({QStringLiteral("rev-parse"), QStringLiteral("--git-path"),
                                    QStringLiteral("hooks/commit-msg")});
    QFile file(QDir(git.workDir()).filePath(QString::fromUtf8(hook.out.left(hook.out.indexOf('\n'))).trimmed()));
    return file.open(QIODevice::ReadOnly) && file.readAll().contains("Change-Id");
}

QString reviewRefspecOptions(const ReviewOptions &options, QString *error)
{
    // Commas separate options and '%' starts them; the rest would make an
    // invalid ref name (see git check-ref-format).
    static const QRegularExpression invalid(QStringLiteral("[\\s,%~^:?*\\[\\\\\\x00-\\x1f\\x7f]|\\.\\.|@\\{"));
    QStringList parts;
    const auto add = [&](const QString &key, const QString &value) {
        if (value.contains(invalid) && error->isEmpty())
            *error = tr("\u201c%1\u201d cannot be sent with a push for review: leave out spaces, commas and "
                        "any of %~^:?*[\\.").arg(value);
        parts.append(key + QLatin1Char('=') + value);
    };
    error->clear();
    if (!options.topic.trimmed().isEmpty())
        add(QStringLiteral("topic"), options.topic.trimmed());
    if (options.state == ReviewOptions::State::Wip)
        parts.append(QStringLiteral("wip"));
    else if (options.state == ReviewOptions::State::Ready)
        parts.append(QStringLiteral("ready"));
    for (const QString &reviewer : options.reviewers)
        add(QStringLiteral("r"), reviewer.trimmed());
    for (const QString &tag : options.hashtags)
        add(QStringLiteral("t"), tag.trimmed());
    if (!error->isEmpty() || parts.isEmpty())
        return {};
    return QLatin1Char('%') + parts.join(QLatin1Char(','));
}

namespace {

class GerritProvider : public ReviewProvider
{
public:
    ReviewHost host() const override { return ReviewHost::Gerrit; }
    QString name() const override { return QStringLiteral("Gerrit"); }
    bool pushesForReview() const override { return true; }

    void completeTarget(const GitRunner &git, PushTarget &target) const override
    {
        if (!target.branch.isEmpty())
            target.savedTopic = config(git, topicKey(target.branch));
    }

    void applyOptions(PushTarget &target, const ReviewOptions &options) const override
    {
        QString error;
        const QString suffix = reviewRefspecOptions(options, &error);
        if (!error.isEmpty()) {
            target.error = error;
            return;
        }
        if (target.args.isEmpty())
            return;
        target.args.last() += suffix;
        target.review = options;
    }

    QString preview(const PushTarget &target) const override
    {
        return target.args.value(target.args.size() - 1);
    }

    // A push with options remembers its topic in branch.<name>.igittTopic.
    void afterPush(const GitRunner &git, const PushTarget &target, NetworkResult &) const override
    {
        if (!target.review || target.branch.isEmpty())
            return;
        const QString topic = target.review->topic.trimmed();
        if (topic.isEmpty())
            git.run({QStringLiteral("config"), QStringLiteral("--unset"), topicKey(target.branch)});
        else
            git.run({QStringLiteral("config"), topicKey(target.branch), topic});
    }

    ReviewQuery query(const GitRunner &git, const ReviewContext &context) const override
    {
        return queryGerritReviews(git, context.remote, context.upstream, context.branch);
    }
};

} // namespace

const ReviewProvider &gerritProvider()
{
    static const GerritProvider provider;
    return provider;
}
