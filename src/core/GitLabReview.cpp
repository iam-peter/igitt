#include "GitLabReview.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include <algorithm>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("GitLabReview", text);
}

BranchReview::Checks pipelineChecks(const QString &status)
{
    if (status == QLatin1StringView("success"))
        return BranchReview::Checks::Success;
    if (status == QLatin1StringView("failed") || status == QLatin1StringView("canceled"))
        return BranchReview::Checks::Failure;
    if (status.isEmpty() || status == QLatin1StringView("skipped"))
        return BranchReview::Checks::None;
    return BranchReview::Checks::Pending; // created, pending, running, manual, scheduled, ...
}

class GitLabProvider : public ReviewProvider
{
public:
    ReviewHost host() const override { return ReviewHost::GitLab; }
    QString name() const override { return QStringLiteral("GitLab"); }

    void applyOptions(PushTarget &target, const ReviewOptions &options) const override
    {
        QString error;
        const QStringList pushOptions = gitLabPushOptions(options, &error);
        if (!error.isEmpty()) {
            if (target.error.isEmpty())
                target.error = error;
            return;
        }
        QStringList args;
        for (const QString &option : pushOptions)
            args << QStringLiteral("-o") << option;
        target.args = args + target.args;
        target.review = options;
    }

    QString preview(const PushTarget &target) const override
    {
        return QStringLiteral("git push ") + shellCommand(target.args);
    }

    ReviewQuery query(const GitRunner &git, const ReviewContext &context) const override
    {
        ReviewQuery query;
        query.host = host();
        query.branch = context.branch;
        const RemoteUrl url = parseRemoteUrl(remoteUrl(git, context.remote));
        if (!url.isValid()) {
            query.error = tr("%1 is not a GitLab repository URL.").arg(remoteUrl(git, context.remote));
            return query;
        }
        const QString project = QStringLiteral("projects/")
                              + QString::fromLatin1(QUrl::toPercentEncoding(url.path));
        const auto api = [&](const QString &endpoint, QByteArray *out, QString *error) {
            return runReviewTool(git, QStringLiteral("glab"),
                                 {QStringLiteral("api"), QStringLiteral("--hostname"), url.host, endpoint},
                                 environment(), name(), missingTool(), out, error);
        };

        QByteArray out;
        if (!api(project + QStringLiteral("/merge_requests?source_branch=")
                     + QString::fromLatin1(QUrl::toPercentEncoding(context.branch))
                     + QStringLiteral("&state=all&order_by=updated_at&per_page=20"),
                 &out, &query.error)) {
            return query;
        }
        query.branchReview = parseGitLabMergeRequests(out, &query.error);
        query.ok = query.error.isEmpty();
        if (!query.branchReview)
            return query;

        // The list leaves out the pipeline; approvals and threads are
        // asked for separately. Each is just left unknown if that fails.
        BranchReview &review = *query.branchReview;
        const QString mr = project + QStringLiteral("/merge_requests/") + QString::number(review.number);
        QString ignored;
        if (api(mr, &out, &ignored))
            readGitLabMergeRequest(review, QJsonDocument::fromJson(out).object());
        if (review.state == BranchReview::State::Open) {
            if (api(mr + QStringLiteral("/approvals"), &out, &ignored))
                readGitLabApprovals(review, out);
            if (api(mr + QStringLiteral("/discussions?per_page=100"), &out, &ignored))
                review.unresolved = countGitLabUnresolved(out);
        }
        return query;
    }

private:
    static QProcessEnvironment environment()
    {
        QProcessEnvironment env;
        env.insert(QStringLiteral("NO_PROMPT"), QStringLiteral("1"));
        return env;
    }

    static QString missingTool()
    {
        return tr("Showing GitLab merge requests needs the GitLab CLI, glab, which is not installed. "
                  "Install it and sign in once with “glab auth login”.");
    }
};

} // namespace

std::optional<BranchReview> parseGitLabMergeRequests(const QByteArray &json, QString *error)
{
    error->clear();
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray()) {
        const QString message = document.object().value(QLatin1StringView("message")).toVariant().toString();
        *error = message.isEmpty() ? tr("GitLab's answer is not a list of merge requests.")
                                   : tr("GitLab: %1").arg(message);
        return std::nullopt;
    }
    const QJsonArray list = document.array();
    if (list.isEmpty())
        return std::nullopt;
    QJsonObject chosen = list.first().toObject();
    for (const QJsonValue &value : list) {
        if (value.toObject().value(QLatin1StringView("state")).toString() == QLatin1StringView("opened")) {
            chosen = value.toObject();
            break;
        }
    }
    BranchReview review;
    review.host = ReviewHost::GitLab;
    readGitLabMergeRequest(review, chosen);
    return review;
}

void readGitLabMergeRequest(BranchReview &review, const QJsonObject &mr)
{
    if (mr.isEmpty())
        return;
    review.host = ReviewHost::GitLab;
    review.number = mr.value(QLatin1StringView("iid")).toInt();
    review.title = mr.value(QLatin1StringView("title")).toString();
    review.url = mr.value(QLatin1StringView("web_url")).toString();
    review.target = mr.value(QLatin1StringView("target_branch")).toString();
    review.headSha = mr.value(QLatin1StringView("sha")).toString();
    const QString state = mr.value(QLatin1StringView("state")).toString();
    review.state = state == QLatin1StringView("merged")  ? BranchReview::State::Merged
                 : state == QLatin1StringView("opened") ? BranchReview::State::Open
                                                        : BranchReview::State::Closed; // closed, locked
    // "work_in_progress" before GitLab 14.
    review.draft = mr.value(QLatin1StringView("draft")).toBool()
                || mr.value(QLatin1StringView("work_in_progress")).toBool();
    review.conflicts = mr.value(QLatin1StringView("has_conflicts")).toBool();
    const QJsonObject pipeline = mr.value(QLatin1StringView("head_pipeline")).toObject();
    if (!pipeline.isEmpty()) {
        const QString status = pipeline.value(QLatin1StringView("status")).toString();
        review.checks = pipelineChecks(status);
        review.checkResults = {{tr("pipeline"), status}};
    }
}

void readGitLabApprovals(BranchReview &review, const QByteArray &json)
{
    const QJsonObject approvals = QJsonDocument::fromJson(json).object();
    if (approvals.isEmpty())
        return;
    review.approvedBy.clear();
    for (const QJsonValue &value : approvals.value(QLatin1StringView("approved_by")).toArray()) {
        const QJsonObject user = value.toObject().value(QLatin1StringView("user")).toObject();
        const QString name = user.value(QLatin1StringView("username")).toString();
        review.approvedBy << (name.isEmpty() ? user.value(QLatin1StringView("name")).toString() : name);
    }
    review.approvalsLeft = approvals.value(QLatin1StringView("approvals_left")).toInt();
    // GitLab calls a merge request that needs no approvals approved; only
    // someone's approval counts here.
    review.decision = review.approvalsLeft > 0 ? BranchReview::Decision::ReviewRequired
                    : !review.approvedBy.isEmpty() ? BranchReview::Decision::Approved
                                                   : BranchReview::Decision::None;
}

std::optional<int> countGitLabUnresolved(const QByteArray &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray())
        return std::nullopt;
    int unresolved = 0;
    for (const QJsonValue &discussion : document.array()) {
        const QJsonArray notes = discussion.toObject().value(QLatin1StringView("notes")).toArray();
        unresolved += std::any_of(notes.cbegin(), notes.cend(), [](const QJsonValue &note) {
            const QJsonObject n = note.toObject();
            return n.value(QLatin1StringView("resolvable")).toBool()
                && !n.value(QLatin1StringView("resolved")).toBool();
        }) ? 1 : 0;
    }
    return unresolved;
}

QStringList gitLabPushOptions(const ReviewOptions &options, QString *error)
{
    error->clear();
    QStringList result;
    const auto add = [&](const QString &key, const QString &value = {}) {
        if (value.contains(QLatin1Char('\n')) && error->isEmpty())
            *error = tr("“%1” cannot be sent as a push option: it has a line break.").arg(value);
        result << (value.isEmpty() ? key : key + QLatin1Char('=') + value);
    };
    if (options.create)
        add(QStringLiteral("merge_request.create"));
    if (!options.target.trimmed().isEmpty())
        add(QStringLiteral("merge_request.target"), options.target.trimmed());
    if (!options.title.trimmed().isEmpty())
        add(QStringLiteral("merge_request.title"), options.title.trimmed());
    if (options.state == ReviewOptions::State::Wip)
        add(QStringLiteral("merge_request.draft"));
    for (const QString &label : options.labels)
        add(QStringLiteral("merge_request.label"), label.trimmed());
    if (options.removeSourceBranch)
        add(QStringLiteral("merge_request.remove_source_branch"));
    if (!error->isEmpty())
        return {};
    return result;
}

const ReviewProvider &gitLabProvider()
{
    static const GitLabProvider provider;
    return provider;
}
