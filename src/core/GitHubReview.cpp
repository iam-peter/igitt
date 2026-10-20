#include "GitHubReview.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("GitHubReview", text);
}

QJsonValue at(const QJsonValue &value, std::initializer_list<const char *> path)
{
    QJsonValue v = value;
    for (const char *key : path)
        v = v.toObject().value(QLatin1StringView(key));
    return v;
}

// A check run or commit status as a word: "passed", "failed", "running", ...
QString checkResult(const QJsonObject &check)
{
    if (check.value(QLatin1StringView("__typename")).toString() == QLatin1StringView("StatusContext")) {
        const QString state = check.value(QLatin1StringView("state")).toString();
        if (state == QLatin1StringView("SUCCESS"))
            return QStringLiteral("passed");
        if (state == QLatin1StringView("FAILURE") || state == QLatin1StringView("ERROR"))
            return QStringLiteral("failed");
        return QStringLiteral("pending");
    }
    if (check.value(QLatin1StringView("status")).toString() != QLatin1StringView("COMPLETED"))
        return QStringLiteral("running");
    const QString conclusion = check.value(QLatin1StringView("conclusion")).toString();
    if (conclusion == QLatin1StringView("SUCCESS"))
        return QStringLiteral("passed");
    if (conclusion == QLatin1StringView("NEUTRAL"))
        return QStringLiteral("neutral");
    if (conclusion == QLatin1StringView("SKIPPED"))
        return QStringLiteral("skipped");
    if (conclusion == QLatin1StringView("CANCELLED"))
        return QStringLiteral("canceled");
    if (conclusion == QLatin1StringView("ACTION_REQUIRED"))
        return QStringLiteral("action required");
    if (conclusion == QLatin1StringView("STALE"))
        return QStringLiteral("stale");
    return QStringLiteral("failed"); // FAILURE, TIMED_OUT, STARTUP_FAILURE
}

BranchReview readPullRequest(const QJsonObject &pr)
{
    BranchReview review;
    review.host = ReviewHost::GitHub;
    review.number = pr.value(QLatin1StringView("number")).toInt();
    review.title = pr.value(QLatin1StringView("title")).toString();
    review.url = pr.value(QLatin1StringView("url")).toString();
    review.target = pr.value(QLatin1StringView("baseRefName")).toString();
    review.headSha = pr.value(QLatin1StringView("headRefOid")).toString();
    const QString state = pr.value(QLatin1StringView("state")).toString();
    review.state = state == QLatin1StringView("MERGED") ? BranchReview::State::Merged
                 : state == QLatin1StringView("CLOSED") ? BranchReview::State::Closed
                                                        : BranchReview::State::Open;
    review.draft = pr.value(QLatin1StringView("isDraft")).toBool();
    review.conflicts = pr.value(QLatin1StringView("mergeable")).toString() == QLatin1StringView("CONFLICTING");

    const QString decision = pr.value(QLatin1StringView("reviewDecision")).toString();
    review.decision = decision == QLatin1StringView("APPROVED")            ? BranchReview::Decision::Approved
                    : decision == QLatin1StringView("CHANGES_REQUESTED") ? BranchReview::Decision::ChangesRequested
                    : decision == QLatin1StringView("REVIEW_REQUIRED")   ? BranchReview::Decision::ReviewRequired
                                                                           : BranchReview::Decision::None;
    for (const QJsonValue &node : at(pr, {"latestReviews", "nodes"}).toArray()) {
        const QString login = at(node, {"author", "login"}).toString();
        const QString reviewState = at(node, {"state"}).toString();
        if (reviewState == QLatin1StringView("APPROVED"))
            review.approvedBy << login;
        else if (reviewState == QLatin1StringView("CHANGES_REQUESTED"))
            review.changesRequestedBy << login;
    }

    const QJsonArray commits = at(pr, {"commits", "nodes"}).toArray();
    const QJsonValue rollup = commits.isEmpty() ? QJsonValue() : at(commits.last(), {"commit", "statusCheckRollup"});
    if (rollup.isObject()) {
        const QString overall = at(rollup, {"state"}).toString();
        review.checks = overall == QLatin1StringView("SUCCESS") ? BranchReview::Checks::Success
                      : overall == QLatin1StringView("FAILURE") || overall == QLatin1StringView("ERROR")
                          ? BranchReview::Checks::Failure
                          : BranchReview::Checks::Pending;
        for (const QJsonValue &node : at(rollup, {"contexts", "nodes"}).toArray()) {
            const QJsonObject check = node.toObject();
            QString name = check.value(QLatin1StringView("name")).toString();
            if (name.isEmpty())
                name = check.value(QLatin1StringView("context")).toString();
            review.checkResults.append({name, checkResult(check)});
        }
    }

    const QJsonValue threads = at(pr, {"reviewThreads", "nodes"});
    if (threads.isArray()) {
        int unresolved = 0;
        for (const QJsonValue &thread : threads.toArray())
            unresolved += thread.toObject().value(QLatin1StringView("isResolved")).toBool() ? 0 : 1;
        review.unresolved = unresolved;
    }
    return review;
}

class GitHubProvider : public ReviewProvider
{
public:
    ReviewHost host() const override { return ReviewHost::GitHub; }
    QString name() const override { return QStringLiteral("GitHub"); }

    // GitHub takes no push options; a pull request is opened after the push.
    void applyOptions(PushTarget &target, const ReviewOptions &options) const override
    {
        if (options.create && target.branch.isEmpty() && target.error.isEmpty())
            target.error = tr("HEAD is detached; a pull request needs a branch.");
        target.review = options;
    }

    QString preview(const PushTarget &target) const override
    {
        QString text = QStringLiteral("git push ") + shellCommand(target.args);
        if (target.review && target.review->create)
            text += QStringLiteral("\ngh pr create ") + shellCommand(gitHubCreateArgs(target));
        return text;
    }

    void afterPush(const GitRunner &git, const PushTarget &target, NetworkResult &result) const override
    {
        if (!target.review || !target.review->create)
            return;
        const RemoteUrl url = parseRemoteUrl(remoteUrl(git, target.remote));
        if (!url.isValid()) {
            result.warning = tr("Pushed, but %1 is no GitHub repository to open a pull request in.").arg(target.remote);
            return;
        }
        QByteArray out;
        QString error;
        if (!runReviewTool(git, QStringLiteral("gh"),
                           QStringList{QStringLiteral("pr"), QStringLiteral("create"), QStringLiteral("--repo"),
                                       url.host + QLatin1Char('/') + url.path}
                               + gitHubCreateArgs(target),
                           environment(), name(), missingTool(), &out, &error)) {
            result.warning = tr("Pushed, but opening the pull request failed: %1").arg(error);
            return;
        }
        // gh prints the new pull request's URL.
        for (const QByteArray &line : out.split('\n')) {
            if (line.startsWith("https://"))
                result.links.append(QString::fromUtf8(line.trimmed()));
        }
    }

    ReviewQuery query(const GitRunner &git, const ReviewContext &context) const override
    {
        ReviewQuery query;
        query.host = host();
        query.branch = context.branch;
        const RemoteUrl url = parseRemoteUrl(remoteUrl(git, context.remote));
        const QStringList ownerAndName = url.path.split(QLatin1Char('/'));
        if (!url.isValid() || ownerAndName.size() != 2) {
            query.error = tr("%1 is not a GitHub repository URL.").arg(remoteUrl(git, context.remote));
            return query;
        }
        QByteArray out;
        if (!runReviewTool(git, QStringLiteral("gh"),
                           {QStringLiteral("api"), QStringLiteral("graphql"), QStringLiteral("--hostname"),
                            url.host == QLatin1StringView("ssh.github.com") ? QStringLiteral("github.com") : url.host,
                            QStringLiteral("-f"), QStringLiteral("query=") + gitHubQuery(),
                            QStringLiteral("-f"), QStringLiteral("owner=") + ownerAndName[0],
                            QStringLiteral("-f"), QStringLiteral("name=") + ownerAndName[1],
                            QStringLiteral("-f"), QStringLiteral("branch=") + context.branch},
                           environment(), name(), missingTool(), &out, &query.error)) {
            return query;
        }
        query.branchReview = parseGitHubPullRequests(out, ownerAndName[0], &query.error);
        query.ok = query.error.isEmpty();
        return query;
    }

private:
    static QProcessEnvironment environment()
    {
        QProcessEnvironment env;
        env.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
        env.insert(QStringLiteral("GH_NO_UPDATE_NOTIFIER"), QStringLiteral("1"));
        return env;
    }

    static QString missingTool()
    {
        return tr("Showing GitHub pull requests needs the GitHub CLI, gh, which is not installed. "
                  "Install it and sign in once with “gh auth login”.");
    }
};

} // namespace

QString gitHubQuery()
{
    return QStringLiteral(
        "fragment pr on PullRequest {"
        " number title url state isDraft mergeable headRefOid baseRefName reviewDecision"
        " headRepositoryOwner { login }"
        " latestReviews(first: 50) { nodes { state author { login } } }"
        " reviewThreads(first: 100) { nodes { isResolved } }"
        " commits(last: 1) { nodes { commit { statusCheckRollup { state contexts(first: 100) { nodes {"
        " __typename ... on CheckRun { name status conclusion } ... on StatusContext { context state }"
        " } } } } } }"
        " }"
        " query($owner: String!, $name: String!, $branch: String!) {"
        " repository(owner: $owner, name: $name) {"
        " pullRequests(headRefName: $branch, first: 10, orderBy: {field: UPDATED_AT, direction: DESC}) {"
        " nodes { ...pr } }"
        " parent { pullRequests(headRefName: $branch, first: 10, orderBy: {field: UPDATED_AT, direction: DESC}) {"
        " nodes { ...pr } } }"
        " } }");
}

std::optional<BranchReview> parseGitHubPullRequests(const QByteArray &json, const QString &owner, QString *error)
{
    error->clear();
    QJsonParseError parseError;
    const QJsonObject answer = QJsonDocument::fromJson(json, &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        *error = tr("GitHub's answer is not JSON: %1").arg(parseError.errorString());
        return std::nullopt;
    }
    const QJsonArray errors = answer.value(QLatin1StringView("errors")).toArray();
    if (!errors.isEmpty()) {
        *error = tr("GitHub: %1").arg(errors.first().toObject().value(QLatin1StringView("message")).toString());
        return std::nullopt;
    }
    const QJsonValue repository = at(answer, {"data", "repository"});
    if (!repository.isObject()) {
        *error = tr("GitHub has no repository %1 that you can see.").arg(owner);
        return std::nullopt;
    }

    // The branch's own pull requests, then those for the repository it was
    // forked from; a branch of the same name in someone else's fork does
    // not count.
    QList<QJsonObject> candidates;
    for (const QJsonValue &list : {at(repository, {"pullRequests", "nodes"}),
                                   at(repository, {"parent", "pullRequests", "nodes"})}) {
        for (const QJsonValue &node : list.toArray()) {
            if (at(node, {"headRepositoryOwner", "login"}).toString().compare(owner, Qt::CaseInsensitive) == 0)
                candidates.append(node.toObject());
        }
    }
    if (candidates.isEmpty())
        return std::nullopt;
    for (const QJsonObject &pr : std::as_const(candidates)) {
        if (pr.value(QLatin1StringView("state")).toString() == QLatin1StringView("OPEN"))
            return readPullRequest(pr);
    }
    return readPullRequest(candidates.first());
}

QStringList gitHubCreateArgs(const PushTarget &target)
{
    QStringList args{QStringLiteral("--head"), target.remoteBranch};
    if (!target.review)
        return args;
    const ReviewOptions &options = *target.review;
    if (!options.target.trimmed().isEmpty())
        args << QStringLiteral("--base") << options.target.trimmed();
    if (options.title.trimmed().isEmpty())
        args << QStringLiteral("--fill");
    else
        args << QStringLiteral("--title") << options.title.trimmed() << QStringLiteral("--body") << QString();
    if (options.state == ReviewOptions::State::Wip)
        args << QStringLiteral("--draft");
    if (!options.reviewers.isEmpty())
        args << QStringLiteral("--reviewer") << options.reviewers.join(QLatin1Char(','));
    if (!options.labels.isEmpty())
        args << QStringLiteral("--label") << options.labels.join(QLatin1Char(','));
    return args;
}

const ReviewProvider &gitHubProvider()
{
    static const GitHubProvider provider;
    return provider;
}
