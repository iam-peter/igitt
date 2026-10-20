#include "Repository.h"
#include "core/GerritReview.h"
#include "core/GitHubReview.h"
#include "core/GitLabReview.h"
#include "core/RemoteOps.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// JSON written with single quotes, which moc tells apart from the ends of
// string literals better than raw strings.
static QByteArray operator""_json(const char *text, size_t size)
{
    return QByteArray(text, qsizetype(size)).replace('\'', '"');
}

class TestRemote : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Test");
        qputenv("GIT_AUTHOR_EMAIL", "test@example.com");
        qputenv("GIT_COMMITTER_NAME", "Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.com");
    }

    // A bare "server", our clone, and someone else's clone.
    void init()
    {
        m_root.reset(new QTemporaryDir);
        QVERIFY(m_root->isValid());
        m_remote = m_root->filePath(u"remote.git"_s);
        GitRunner(m_root->path()).run({"init", "-q", "--bare", "-b", "main", m_remote});
        QVERIFY(QDir(m_root->path()).mkdir(u"work"_s));
        m_work.setWorkDir(m_root->filePath(u"work"_s));
        work({"init", "-q", "-b", "main"});
        commitIn(m_work, "base");
        work({"remote", "add", "origin", m_remote});
        work({"push", "-q", "-u", "origin", "main"});
        GitRunner(m_root->path()).run({"clone", "-q", m_remote, m_root->filePath(u"other"_s)});
        m_other.setWorkDir(m_root->filePath(u"other"_s));
        m_repo.reset(new Repository);
        m_errors.reset(new QSignalSpy(m_repo.get(), &Repository::errorOccurred));
    }

    void cleanup()
    {
        if (m_errors && !m_errors->isEmpty())
            qWarning() << "errors:" << *m_errors;
        m_errors.reset();
        m_repo.reset();
    }

    void pushesToTheUpstream()
    {
        commitIn(m_work, "mine");
        open();
        QVERIFY(m_repo->canPush());
        QVERIFY(!m_repo->pushForReview());
        QVERIFY(m_repo->pushDescription().contains("origin/main"_L1));
        QCOMPARE(m_repo->ahead(), 1);

        m_repo->push();
        QVERIFY(settle());
        QCOMPARE(remoteRev(u"refs/heads/main"_s), rev(m_work, u"HEAD"_s));
        QCOMPARE(m_repo->ahead(), 0);
        QVERIFY(m_errors->isEmpty());
    }

    void firstPushSetsTheUpstream()
    {
        work({"checkout", "-q", "-b", "topic"});
        commitIn(m_work, "topic work");
        open();
        QVERIFY(m_repo->pushDescription().contains("track"_L1));

        m_repo->push();
        QVERIFY(settle());
        QCOMPARE(remoteRev(u"refs/heads/topic"_s), rev(m_work, u"HEAD"_s));
        QCOMPARE(work({"config", "branch.topic.remote"}).trimmed(), "origin"_ba);
    }

    void fetches()
    {
        commitIn(m_other, "theirs");
        GitRunner(m_other).run({"push", "-q", "origin", "main"});
        open();
        QVERIFY(!m_repo->lastFetch().isValid());

        m_repo->fetch();
        QVERIFY(settle());
        QCOMPARE(rev(m_work, u"origin/main"_s), rev(m_other, u"HEAD"_s));
        QCOMPARE(m_repo->behind(), 1);
        QVERIFY(m_repo->lastFetch().isValid());
    }

    void rejectedPushOffersAForcePushWithLease()
    {
        commitIn(m_other, "theirs");
        m_other.run({"push", "-q", "origin", "main"});
        commitIn(m_work, "mine");
        open();

        QSignalSpy rejected(m_repo.get(), &Repository::pushRejected);
        m_repo->push();
        QVERIFY(settle());
        QCOMPARE(rejected.size(), 1);
        QVERIFY(rejected.first().at(1).toString().contains("origin/main"_L1));

        // Not fetched: the lease protects their commit.
        m_repo->push(true);
        QVERIFY(settle());
        QCOMPARE(m_errors->size(), 1);
        m_errors->clear();
        QCOMPARE(remoteRev(u"refs/heads/main"_s), rev(m_other, u"HEAD"_s));

        // Seen, so overwriting it is a decision.
        m_repo->fetch();
        m_repo->push(true);
        QVERIFY(settle());
        QCOMPARE(remoteRev(u"refs/heads/main"_s), rev(m_work, u"HEAD"_s));
    }

    void pushesForReviewToGerrit()
    {
        work({"config", "--add", "remote.origin.push", "refs/heads/*:refs/for/*"});
        commitIn(m_work, "for review");
        open();
        QVERIFY(m_repo->pushForReview());
        QVERIFY(m_repo->pushDescription().contains("refs/for/main"_L1));

        m_repo->push();
        QVERIFY(settle());
        QCOMPARE(remoteRev(u"refs/for/main"_s), rev(m_work, u"HEAD"_s));
        QVERIFY(remoteRev(u"refs/heads/main"_s) != rev(m_work, u"HEAD"_s)); // under review, not merged

        m_repo->setGerritBranch(u"6.8"_s);
        QVERIFY(settle());
        QVERIFY(m_repo->pushDescription().contains("refs/for/6.8"_L1));
        m_repo->push();
        QVERIFY(settle());
        QCOMPARE(remoteRev(u"refs/for/6.8"_s), rev(m_work, u"HEAD"_s));
    }

    void recognizesGerritByItsHookAndAllowsOverrides()
    {
        GitRunner &git = m_work;
        QVERIFY(!looksLikeGerrit(git, u"origin"_s));
        QFile hook(m_work.workDir() + u"/.git/hooks/commit-msg"_s);
        QVERIFY(hook.open(QIODevice::WriteOnly));
        hook.write("#!/bin/sh\n# adds a Change-Id line\n");
        hook.close();
        QVERIFY(looksLikeGerrit(git, u"origin"_s));

        QVERIFY(resolvePushTarget(git, {PushMode::Plain, {}}).args.contains(u"HEAD:refs/heads/main"_s));
        // Gerrit works from a detached HEAD, given a branch.
        work({"checkout", "-q", "--detach"});
        QVERIFY(!resolvePushTarget(git, {PushMode::Plain, {}}).error.isEmpty());
        const PushTarget review = resolvePushTarget(git, {PushMode::Gerrit, u"dev"_s});
        QVERIFY(review.error.isEmpty());
        QVERIFY(review.args.contains(u"HEAD:refs/for/dev"_s));
    }

    void staysOffWhilePlanning()
    {
        commitIn(m_work, "mine");
        open();
        m_repo->startPlanning();
        QVERIFY(settle());
        m_repo->push();
        m_repo->fetch();
        QVERIFY(settle());
        QCOMPARE(m_errors->size(), 2);
        m_errors->clear();
        QVERIFY(remoteRev(u"refs/heads/main"_s) != rev(m_work, u"HEAD"_s));
    }

    void needsARemote()
    {
        work({"remote", "remove", "origin"});
        open();
        QVERIFY(!m_repo->canPush());
        QVERIFY(m_repo->pushDescription().contains("No remote"_L1));
    }

    void parsesGerritRemotesAndQueries()
    {
        const GerritRemote remote = parseGerritRemote(u"ssh://henning@codereview.qt-project.org:29418/qt/qtbase"_s);
        QCOMPARE(remote.user, u"henning"_s);
        QCOMPARE(remote.host, u"codereview.qt-project.org"_s);
        QCOMPARE(remote.port, 29418);
        QCOMPARE(remote.project, u"qt/qtbase"_s);
        QCOMPARE(parseGerritRemote(u"ssh://review.example/a/b.git"_s).project, u"a/b"_s);
        QVERIFY(!parseGerritRemote(u"https://example.org/x.git"_s).isValid());

        const QHash<QString, GerritChange> changes = parseGerritQuery(
            R"({"project":"p","branch":"main","id":"I1","number":101,"url":"https://r/c/p/+/101","status":"NEW","currentPatchSet":{"number":3,"revision":"aaa"}})"
            "\n"
            R"({"type":"stats","rowCount":1})"_ba);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes[u"I1"_s].number, 101);
        QCOMPARE(changes[u"I1"_s].patchSet, 3);
        QCOMPARE(changes[u"I1"_s].revision, u"aaa"_s);
        QVERIFY(!changes[u"I1"_s].wip);
        QVERIFY(changes[u"I1"_s].votes.isEmpty());

        // Votes, comments and the rest that --current-patch-set --comments adds.
        const GerritChange voted = parseGerritQuery(
            R"({"id":"I2","number":102,"status":"NEW","wip":true,"topic":"speedup","hashtags":["perf","6.9"],)"
            R"("currentPatchSet":{"number":4,"revision":"bbb","approvals":[)"
            R"({"type":"Code-Review","value":"2","by":{"name":"Alice","username":"alice"}},)"
            R"({"type":"Code-Review","value":"-1","by":{"username":"bob"}},)"
            R"({"type":"Verified","value":"1","by":{"name":"Qt CI Bot"}},)"
            R"({"type":"Sanity-Review","value":"1","by":{"name":"Sanity Bot"}}],)"
            R"("comments":[{"file":"a.cpp","line":3,"message":"x"},{"file":"b.cpp","line":7,"message":"y"}]}})"_ba)
            .value(u"I2"_s);
        QVERIFY(voted.wip);
        QCOMPARE(voted.topic, u"speedup"_s);
        QCOMPARE(voted.hashtags, (QStringList{u"perf"_s, u"6.9"_s}));
        QCOMPARE(voted.votes.size(), 4);
        QCOMPARE(voted.votes[1].value, -1);
        QCOMPARE(voted.votes[1].by, u"bob"_s);
        QCOMPARE(voted.comments, 2);

        ReviewInfo info;
        info.patchSet = voted.patchSet;
        info.wip = voted.wip;
        info.topic = voted.topic;
        info.hashtags = voted.hashtags;
        info.votes = voted.votes;
        info.comments = voted.comments;
        info.unresolved = true;
        QCOMPARE(info.voteRange(u"Code-Review"_s), std::pair(-1, 2));
        QCOMPARE(info.shownVote(u"Code-Review"_s), -1); // a veto shows
        QCOMPARE(info.shownVote(u"Verified"_s), 1);
        QCOMPARE(info.badges(), u"WIP · CR-1 · V+1 · unresolved"_s);
        const QString details = info.details();
        QVERIFY2(details.contains("Code-Review -1 / +2: +2 Alice, -1 bob"_L1), qPrintable(details));
        QVERIFY(details.contains("Verified +1: +1 Qt CI Bot"_L1));
        QVERIFY(details.contains("Sanity-Review +1"_L1));
        QVERIFY(details.contains("Topic: speedup"_L1));
        QVERIFY(details.contains("Hashtags: perf, 6.9"_L1));
        QVERIFY(details.contains("unresolved"_L1));
        QVERIFY(details.contains("Inline comments on patch set 4: 2"_L1));
        QVERIFY(ReviewInfo{}.badges().isEmpty());
    }

    void buildsGerritPushOptions()
    {
        QString error;
        QCOMPARE(reviewRefspecOptions({}, &error), QString());
        ReviewOptions options;
        options.topic = u" speedup "_s;
        options.state = ReviewOptions::State::Wip;
        options.reviewers = splitReviewList(u"a@example.org, b@example.org"_s);
        options.hashtags = splitReviewList(u"perf 6.9"_s);
        QCOMPARE(reviewRefspecOptions(options, &error), u"%topic=speedup,wip,r=a@example.org,r=b@example.org,t=perf,t=6.9"_s);
        QVERIFY(error.isEmpty());
        options = {};
        options.state = ReviewOptions::State::Ready;
        QCOMPARE(reviewRefspecOptions(options, &error), u"%ready"_s);

        for (const QString &bad : {u"two words"_s, u"a,b"_s, u"x..y"_s, u"50%"_s, u"q?"_s}) {
            options.topic = bad;
            QVERIFY(reviewRefspecOptions(options, &error).isEmpty());
            QVERIFY2(error.contains(bad), qPrintable(error));
        }
    }

    void pushesForReviewWithOptions()
    {
        work({"config", "--add", "remote.origin.push", "refs/heads/*:refs/for/*"});
        // The server prints a change link, as Gerrit does.
        QFile hook(m_remote + u"/hooks/post-receive"_s);
        QVERIFY(hook.open(QIODevice::WriteOnly));
        hook.write("#!/bin/sh\necho\necho '  https://review.example/c/proj/+/101 for review [WIP]'\n");
        hook.close();
        QVERIFY(hook.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        commitIn(m_work, "for review");
        open();
        QVERIFY(m_repo->reviewTopic().isEmpty());

        const QVariantMap options{{u"topic"_s, u"speedup"_s}, {u"state"_s, u"wip"_s},
                                  {u"reviewers"_s, u"a@example.org"_s}, {u"hashtags"_s, u"perf"_s}};
        const QString refspec = u"refs/for/main%topic=speedup,wip,r=a@example.org,t=perf"_s;
        QCOMPARE(m_repo->pushPreview(options).value(u"text"_s).toString(), u"HEAD:"_s + refspec);

        QSignalSpy notices(m_repo.get(), &Repository::notice);
        m_repo->push(false, options);
        QVERIFY(settle());
        QCOMPARE(remoteRev(refspec), rev(m_work, u"HEAD"_s));
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().at(1).toStringList(), QStringList{u"https://review.example/c/proj/+/101"_s});
        QVERIFY(!notices.first().at(0).toString().contains("https"_L1));
        // The branch remembers its topic.
        QCOMPARE(work({"config", "branch.main.igittTopic"}).trimmed(), "speedup"_ba);
        QCOMPARE(m_repo->reviewTopic(), u"speedup"_s);

        // A plain push leaves it, one without a topic forgets it.
        m_repo->push();
        QVERIFY(settle());
        QCOMPARE(m_repo->reviewTopic(), u"speedup"_s);
        m_repo->push(false, {{u"state"_s, u"ready"_s}});
        QVERIFY(settle());
        QCOMPARE(remoteRev(u"refs/for/main%ready"_s), rev(m_work, u"HEAD"_s));
        QVERIFY(m_repo->reviewTopic().isEmpty());

        // What a refspec cannot carry is refused before pushing.
        const QVariantMap bad{{u"topic"_s, u"two words"_s}};
        QVERIFY(m_repo->pushPreview(bad).value(u"error"_s).toString().contains("two words"_L1));
        m_repo->push(false, bad);
        QVERIFY(settle());
        QCOMPARE(m_errors->size(), 1);
        QVERIFY(m_errors->first().first().toString().contains("two words"_L1));
        m_errors->clear();
        QVERIFY(m_repo->reviewTopic().isEmpty());
    }

    void asksGerritWhetherThereIsAnythingToPush()
    {
        // A stand-in for ssh that answers like Gerrit.
        QTemporaryDir bin;
        QFile ssh(bin.filePath(u"ssh"_s));
        QVERIFY(ssh.open(QIODevice::WriteOnly));
        // It answers the has:unresolved query from a file of its own.
        ssh.write("#!/bin/sh\n"
                  "[ -n \"$FAKE_SSH_FAIL\" ] && { echo 'Connection refused' >&2; exit 255; }\n"
                  "case \"$*\" in *has:unresolved*)\n"
                  "  printf '%s\\n' \"$@\" > \"$FAKE_SSH_ARGS.unresolved\"; cat \"$FAKE_GERRIT_JSON.unresolved\";;\n"
                  "*) printf '%s\\n' \"$@\" > \"$FAKE_SSH_ARGS\"; cat \"$FAKE_GERRIT_JSON\";;\n"
                  "esac\n");
        ssh.close();
        QVERIFY(ssh.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", bin.path().toUtf8() + ':' + path);
        const QString args = bin.filePath(u"args"_s);
        const QString json = bin.filePath(u"answer.json"_s);
        qputenv("FAKE_SSH_ARGS", args.toUtf8());
        qputenv("FAKE_GERRIT_JSON", json.toUtf8());

        work({"remote", "set-url", "origin", "ssh://tester@review.example:29418/proj/sub"});
        commitWithChangeId("first", "I1111111111111111111111111111111111111111");
        commitWithChangeId("second", "I2222222222222222222222222222222222222222");
        const auto answer = [&](const QString &secondRevision) {
            QFile file(json);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            const auto line = [](const char *id, int number, const QString &revision, const char *extra,
                                 const char *approvals) {
                return QStringLiteral(R"({"id":"%1","number":%2,"url":"https://review.example/c/proj/+/%2",)"
                                      R"("status":"NEW",%4"currentPatchSet":{"number":2,"revision":"%3",)"
                                      R"("approvals":[%5],"comments":[{"file":"file.txt","line":1}]}})")
                    .arg(QLatin1StringView(id)).arg(number).arg(revision, QLatin1StringView(extra),
                                                                 QLatin1StringView(approvals))
                    .toUtf8() + '\n';
            };
            file.write(line("I1111111111111111111111111111111111111111", 101, rev(m_work, u"HEAD~1"_s), "",
                            R"({"type":"Code-Review","value":"2","by":{"name":"Alice"}},)"
                            R"({"type":"Verified","value":"1","by":{"name":"Qt CI Bot"}})"));
            file.write(line("I2222222222222222222222222222222222222222", 102, secondRevision,
                            R"("wip":true,"topic":"speedup",)",
                            R"({"type":"Code-Review","value":"1","by":{"name":"Alice"}},)"
                            R"({"type":"Verified","value":"-1","by":{"name":"Qt CI Bot"}})"));
            file.write(R"({"type":"stats","rowCount":2})" "\n");
            QFile unresolved(json + u".unresolved"_s);
            QVERIFY(unresolved.open(QIODevice::WriteOnly | QIODevice::Truncate));
            unresolved.write(R"({"id":"I2222222222222222222222222222222222222222","number":102,"status":"NEW"})" "\n"
                             R"({"type":"stats","rowCount":1})" "\n");
        };

        // Both commits are the current patch sets: nothing to push.
        answer(rev(m_work, u"HEAD"_s));
        open();
        QVERIFY(m_repo->pushForReview());
        QTRY_VERIFY(!m_repo->pushNeeded());
        QVERIFY(m_repo->reviewSummary().contains("2 up to date"_L1));
        const QVariantMap review = m_repo->commits()->data(m_repo->commits()->index(0), CommitModel::ReviewRole).toMap();
        QCOMPARE(review.value(u"state"_s).toString(), u"upToDate"_s);
        QCOMPARE(review.value(u"number"_s).toInt(), 102);
        QFile asked(args);
        QVERIFY(asked.open(QIODevice::ReadOnly));
        const QByteArray query = asked.readAll();
        QVERIFY(query.contains("tester@review.example"));
        QVERIFY(query.contains("project:proj/sub\nbranch:main"));
        QVERIFY(query.contains("change:I2222222222222222222222222222222222222222"));
        QVERIFY(query.contains("--current-patch-set\n--comments"));
        QVERIFY(!query.contains("has:unresolved"));

        // Votes, work in progress and unresolved comments, for the row's tag.
        QCOMPARE(review.value(u"codeReview"_s).toInt(), 1);
        QCOMPARE(review.value(u"verified"_s).toInt(), -1);
        QVERIFY(review.value(u"wip"_s).toBool());
        QCOMPARE(review.value(u"topic"_s).toString(), u"speedup"_s);
        QCOMPARE(review.value(u"comments"_s).toInt(), 1);
        QCOMPARE(review.value(u"unresolved"_s), QVariant(true));
        QCOMPARE(review.value(u"badges"_s).toString(), u"WIP · CR+1 · V-1 · unresolved"_s);
        QVERIFY(review.value(u"details"_s).toString().contains("Verified -1: -1 Qt CI Bot"_L1));
        const QVariantMap older = m_repo->commits()->data(m_repo->commits()->index(1), CommitModel::ReviewRole).toMap();
        QCOMPARE(older.value(u"unresolved"_s), QVariant(false));
        QCOMPARE(older.value(u"badges"_s).toString(), u"CR+2 · V+1"_s);
        QFile askedUnresolved(args + u".unresolved"_s);
        QVERIFY(askedUnresolved.open(QIODevice::ReadOnly));
        const QByteArray unresolvedQuery = askedUnresolved.readAll();
        QVERIFY(unresolvedQuery.contains("project:proj/sub\nbranch:main"));
        QVERIFY(unresolvedQuery.contains("change:I1111111111111111111111111111111111111111\nOR\n"
                                         "change:I2222222222222222222222222222222222222222\n)\nhas:unresolved"));

        // The newest commit changed since its upload: pushing makes sense again.
        answer(u"0123456789abcdef0123456789abcdef01234567"_s);
        m_repo->refresh();
        QTRY_VERIFY(m_repo->pushNeeded());
        QCOMPARE(m_repo->commits()->data(m_repo->commits()->index(0), CommitModel::ReviewRole).toMap()
                     .value(u"state"_s).toString(), u"outdated"_s);

        // Gerrit out of reach: leave pushing to the user.
        qputenv("FAKE_SSH_FAIL", "1");
        answer(rev(m_work, u"HEAD"_s));
        m_repo->refresh();
        QTRY_VERIFY(m_repo->reviewSummary().contains("Connection refused"_L1));
        QVERIFY(m_repo->pushNeeded());

        qunsetenv("FAKE_SSH_FAIL");
        qputenv("PATH", path);
        QVERIFY(settle());
    }

    void detectsReviewHosts()
    {
        const RemoteUrl https = parseRemoteUrl(u"https://github.com/acme/widget.git"_s);
        QCOMPARE(https.host, u"github.com"_s);
        QCOMPARE(https.path, u"acme/widget"_s);
        const RemoteUrl scp = parseRemoteUrl(u"git@gitlab.com:group/sub/proj.git"_s);
        QCOMPARE(scp.user, u"git"_s);
        QCOMPARE(scp.host, u"gitlab.com"_s);
        QCOMPARE(scp.path, u"group/sub/proj"_s);
        const RemoteUrl ssh = parseRemoteUrl(u"ssh://git@gitlab.example.org:2222/group/proj/"_s);
        QCOMPARE(ssh.port, 2222);
        QCOMPARE(ssh.path, u"group/proj"_s);
        for (const QString &local : {u"/srv/repo.git"_s, u"../repo"_s, u"file:///srv/repo.git"_s, u"C:\\repo"_s})
            QVERIFY2(!parseRemoteUrl(local).isValid(), qPrintable(local));

        const GitRunner &git = m_work;
        const auto detect = [&](const QString &url, const PushSettings &settings = {}) {
            work({"remote", "set-url", "origin", url});
            return detectReviewHost(git, u"origin"_s, settings);
        };
        QCOMPARE(detect(u"https://github.com/acme/widget.git"_s), ReviewHost::GitHub);
        QCOMPARE(detect(u"git@github.example.com:acme/widget.git"_s), ReviewHost::GitHub);
        QCOMPARE(detect(u"git@gitlab.com:group/proj.git"_s), ReviewHost::GitLab);
        QCOMPARE(detect(u"ssh://git@gitlab.example.org:2222/group/proj"_s), ReviewHost::GitLab);
        QCOMPARE(detect(m_remote), ReviewHost::None);
        // Self-hosted servers by other names need to be listed.
        QCOMPARE(detect(u"https://git.example.org/group/proj.git"_s), ReviewHost::None);
        QCOMPARE(detect(u"https://git.example.org/group/proj.git"_s, {PushMode::Auto, {}, {}, {u"git.example.org"_s}}),
                 ReviewHost::GitLab);
        QCOMPARE(detect(u"https://git.example.org/group/proj.git"_s, {PushMode::Auto, {}, {u"https://GIT.example.org/"_s}, {}}),
                 ReviewHost::GitHub);
        // The setting decides, if it is not Auto.
        QCOMPARE(detect(u"https://github.com/acme/widget.git"_s, {PushMode::Plain, {}}), ReviewHost::None);
        QCOMPARE(detect(m_remote, {PushMode::GitLab, {}}), ReviewHost::GitLab);
        // Gerrit wins, as before.
        work({"config", "--add", "remote.origin.push", "refs/heads/*:refs/for/*"});
        QCOMPARE(detect(u"https://github.com/acme/widget.git"_s), ReviewHost::Gerrit);
    }

    void parsesGitHubPullRequests()
    {
        QString error;
        // Someone's fork with a branch of the same name, an older pull
        // request of ours, and the open one, in the repository ours was
        // forked from.
        const QByteArray answer = "{'data':{'repository':{"
            "'pullRequests':{'nodes':["
              "{'number':3,'title':'Theirs','state':'OPEN','headRepositoryOwner':{'login':'someone'}},"
              "{'number':5,'title':'Old','state':'CLOSED','headRepositoryOwner':{'login':'acme'}}]},"
            "'parent':{'pullRequests':{'nodes':["
              "{'number':12,'title':'Speed up','url':'https://github.com/up/widget/pull/12','state':'OPEN',"
               "'isDraft':true,'mergeable':'CONFLICTING','headRefOid':'abc123456789','baseRefName':'main',"
               "'reviewDecision':'CHANGES_REQUESTED','headRepositoryOwner':{'login':'ACME'},"
               "'latestReviews':{'nodes':[{'state':'APPROVED','author':{'login':'alice'}},"
                                         "{'state':'CHANGES_REQUESTED','author':{'login':'bob'}},"
                                         "{'state':'COMMENTED','author':{'login':'carol'}}]},"
               "'reviewThreads':{'nodes':[{'isResolved':true},{'isResolved':false}]},"
               "'commits':{'nodes':[{'commit':{'statusCheckRollup':{'state':'FAILURE','contexts':{'nodes':["
                 "{'__typename':'CheckRun','name':'build','status':'COMPLETED','conclusion':'SUCCESS'},"
                 "{'__typename':'CheckRun','name':'lint','status':'COMPLETED','conclusion':'FAILURE'},"
                 "{'__typename':'CheckRun','name':'docs','status':'IN_PROGRESS','conclusion':null},"
                 "{'__typename':'StatusContext','context':'ci/legacy','state':'SUCCESS'}]}}}}]}}]}}"
        "}}}"_json;
        const std::optional<BranchReview> pr = parseGitHubPullRequests(answer, u"acme"_s, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(pr);
        QCOMPARE(pr->number, 12);
        QCOMPARE(pr->label(), u"PR #12"_s);
        QCOMPARE(pr->state, BranchReview::State::Open);
        QVERIFY(pr->draft);
        QVERIFY(pr->conflicts);
        QCOMPARE(pr->decision, BranchReview::Decision::ChangesRequested);
        QCOMPARE(pr->approvedBy, QStringList{u"alice"_s});
        QCOMPARE(pr->changesRequestedBy, QStringList{u"bob"_s});
        QCOMPARE(pr->checks, BranchReview::Checks::Failure);
        QCOMPARE(pr->checkResults.size(), 4);
        QCOMPARE(pr->unresolved, std::optional<int>(1));
        QCOMPARE(pr->badges(), u"draft · changes requested · checks failed · conflicts · 1 unresolved"_s);
        const QString details = pr->details(u"0123456789"_s);
        QVERIFY2(details.contains("Pull request #12: Speed up"_L1), qPrintable(details));
        QVERIFY(details.contains("Open, draft, into main"_L1));
        QVERIFY(details.contains("Approved by alice"_L1));
        QVERIFY(details.contains("Changes requested by bob"_L1));
        QVERIFY(details.contains("Checks: 1 failed, 2 passed, 1 running"_L1));
        QVERIFY(details.contains("failed: lint"_L1));
        QVERIFY(details.contains("1 unresolved thread"_L1));
        QVERIFY(details.contains("It has abc12345, not this commit"_L1));
        QVERIFY(!pr->details(u"abc123456789"_s).contains("not this commit"_L1));
        QVERIFY(pr->summary().startsWith("Pull request #12: open, draft"_L1));

        // Only the closed one of ours: shown, as closed.
        const QByteArray closed = "{'data':{'repository':{'pullRequests':{'nodes':["
            "{'number':5,'state':'MERGED','headRepositoryOwner':{'login':'acme'}}]},'parent':null}}}"_json;
        QCOMPARE(parseGitHubPullRequests(closed, u"acme"_s, &error)->state, BranchReview::State::Merged);
        QCOMPARE(parseGitHubPullRequests(closed, u"acme"_s, &error)->badges(), u"merged"_s);
        // None, and failures.
        QVERIFY(!parseGitHubPullRequests("{'data':{'repository':{'pullRequests':{'nodes':[]}}}}"_json,
                                         u"acme"_s, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!parseGitHubPullRequests("{'errors':[{'message':'Bad credentials'}]}"_json, u"acme"_s, &error));
        QVERIFY(error.contains("Bad credentials"_L1));
        QVERIFY(!parseGitHubPullRequests("{'data':{'repository':null}}"_json, u"acme"_s, &error));
        QVERIFY(!error.isEmpty());
    }

    void parsesGitLabMergeRequests()
    {
        QString error;
        const std::optional<BranchReview> mr = parseGitLabMergeRequests(
            "[{'iid':3,'state':'closed','title':'Old'},"
                "{'iid':7,'state':'opened','title':'Speed up','draft':true,'target_branch':'main','sha':'abc',"
                 "'web_url':'https://gitlab.example.org/g/w/-/merge_requests/7'}]"_json, &error);
        QVERIFY(error.isEmpty());
        QVERIFY(mr);
        BranchReview review = *mr;
        QCOMPARE(review.number, 7);
        QCOMPARE(review.label(), u"MR !7"_s);
        QVERIFY(review.draft);
        QCOMPARE(review.checks, BranchReview::Checks::None);

        readGitLabMergeRequest(review, QJsonDocument::fromJson(
            "{'iid':7,'state':'opened','title':'Speed up','work_in_progress':true,'has_conflicts':false,"
                "'head_pipeline':{'status':'running'}}"_json).object());
        QCOMPARE(review.checks, BranchReview::Checks::Pending);
        readGitLabApprovals(review, "{'approvals_left':1,'approved_by':[{'user':{'name':'Alice A','username':'alice'}}]}"_json);
        QCOMPARE(review.approvedBy, QStringList{u"alice"_s});
        QCOMPARE(review.decision, BranchReview::Decision::ReviewRequired);
        review.unresolved = countGitLabUnresolved(
            "[{'notes':[{'resolvable':false}]},"
                "{'notes':[{'resolvable':true,'resolved':true}]},"
                "{'notes':[{'resolvable':true,'resolved':false},{'resolvable':true,'resolved':true}]}]"_json);
        QCOMPARE(review.unresolved, std::optional<int>(1));
        QCOMPARE(review.badges(), u"draft · 1 approval(s) left · pipeline running · 1 unresolved"_s);
        QVERIFY(review.details({}).contains("Pipeline: 1 running"_L1));
        QVERIFY(review.details({}).contains("1 more approval(s) required"_L1));
        readGitLabApprovals(review, "{'approvals_left':0,'approved_by':[{'user':{'username':'alice'}}]}"_json);
        QCOMPARE(review.decision, BranchReview::Decision::Approved);
        QVERIFY(!countGitLabUnresolved("{'message':'404 Not found'}"_json));

        QVERIFY(!parseGitLabMergeRequests("[]"_ba, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!parseGitLabMergeRequests("{'message':'401 Unauthorized'}"_json, &error));
        QVERIFY(error.contains("401"_L1));

        ReviewOptions options;
        options.create = true;
        options.target = u"main"_s;
        options.title = u"Speed up"_s;
        options.state = ReviewOptions::State::Wip;
        options.labels = {u"perf"_s, u"needs review"_s};
        options.removeSourceBranch = true;
        QCOMPARE(gitLabPushOptions(options, &error),
                 (QStringList{u"merge_request.create"_s, u"merge_request.target=main"_s,
                              u"merge_request.title=Speed up"_s, u"merge_request.draft"_s,
                              u"merge_request.label=perf"_s, u"merge_request.label=needs review"_s,
                              u"merge_request.remove_source_branch"_s}));
        QVERIFY(gitLabPushOptions({}, &error).isEmpty());
        options.title = u"two\nlines"_s;
        QVERIFY(gitLabPushOptions(options, &error).isEmpty());
        QVERIFY(error.contains("line break"_L1));
    }

    void showsTheBranchPullRequestFromGitHub()
    {
        // A stand-in for gh that records how it was called.
        QTemporaryDir bin;
        const QString args = bin.filePath(u"args"_s);
        const QString json = bin.filePath(u"answer.json"_s);
        writeScript(bin.filePath(u"gh"_s),
                    "#!/bin/sh\n"
                    "printf '%s\\n' \"$@\" \"prompt=$GH_PROMPT_DISABLED\" -- >> \"" + args.toUtf8() + "\"\n"
                    "[ -n \"$FAKE_GH_FAIL\" ] && { echo \"$FAKE_GH_FAIL\" >&2; exit 1; }\n"
                    "case \"$1 $2\" in\n"
                    "'pr create') echo 'https://github.com/acme/widget/pull/13';;\n"
                    "*) cat \"" + json.toUtf8() + "\";;\n"
                    "esac\n");
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", bin.path().toUtf8() + ':' + path);
        // Nothing may ask the real one once the stand-in is gone.
        const auto restore = qScopeGuard([this, path] {
            m_repo->setPushMode(u"plain"_s);
            QVERIFY(settle());
            qputenv("PATH", path);
        });

        // A GitHub URL that git pushes to the local "server".
        const QString url = u"https://github.com/acme/widget.git"_s;
        work({"config", "url." + m_remote + ".insteadOf", url});
        work({"remote", "set-url", "origin", url});
        work({"checkout", "-q", "-b", "topic"});
        commitIn(m_work, "topic work");
        work({"push", "-q", "-u", "origin", "topic"});
        commitIn(m_work, "more");
        QFile answer(json);
        QVERIFY(answer.open(QIODevice::WriteOnly));
        answer.write("{'data':{'repository':{'pullRequests':{'nodes':["
            "{'number':12,'title':'Speed up','url':'https://github.com/acme/widget/pull/12','state':'OPEN',"
             "'isDraft':false,'headRefOid':'"_json + rev(m_work, u"HEAD~1"_s).toUtf8() + "','baseRefName':'main',"
             "'reviewDecision':'APPROVED','headRepositoryOwner':{'login':'acme'},"
             "'latestReviews':{'nodes':[{'state':'APPROVED','author':{'login':'alice'}}]},"
             "'reviewThreads':{'nodes':[{'isResolved':false},{'isResolved':false}]},"
             "'commits':{'nodes':[{'commit':{'statusCheckRollup':{'state':'SUCCESS','contexts':{'nodes':["
               "{'__typename':'CheckRun','name':'build','status':'COMPLETED','conclusion':'SUCCESS'}]}}}}]}}]},"
            "'parent':null}}}"_json);
        answer.close();

        open();
        QCOMPARE(m_repo->reviewHost(), u"github"_s);
        QVERIFY(!m_repo->pushForReview());
        QTRY_VERIFY(!m_repo->branchReview().isEmpty());
        const QVariantMap review = m_repo->commits()->data(m_repo->commits()->index(0), CommitModel::ReviewRole).toMap();
        QCOMPARE(review.value(u"kind"_s).toString(), u"pullRequest"_s);
        QCOMPARE(review.value(u"label"_s).toString(), u"PR #12"_s);
        QCOMPARE(review.value(u"state"_s).toString(), u"open"_s);
        QCOMPARE(review.value(u"decision"_s).toString(), u"approved"_s);
        QCOMPARE(review.value(u"checks"_s).toString(), u"success"_s);
        QCOMPARE(review.value(u"unresolved"_s).toInt(), 2);
        QCOMPARE(review.value(u"url"_s).toString(), u"https://github.com/acme/widget/pull/12"_s);
        QCOMPARE(review.value(u"badges"_s).toString(), u"approved · checks passed · 2 unresolved"_s);
        // HEAD is one commit ahead of what the pull request has.
        QVERIFY(review.value(u"details"_s).toString().contains("pushing updates it"_L1));
        QVERIFY(m_repo->reviewSummary().contains("#12"_L1));
        QVERIFY(m_repo->pushNeeded());
        QVERIFY(!m_repo->commits()->data(m_repo->commits()->index(1), CommitModel::ReviewRole).isValid());
        QByteArray asked = readAll(args);
        QVERIFY2(asked.startsWith("api\ngraphql\n--hostname\ngithub.com\n-f\nquery="), asked.constData());
        QVERIFY(asked.contains("\nowner=acme\n-f\nname=widget\n-f\nbranch=topic\nprompt=1\n"));

        // Push and open a pull request.
        const QVariantMap options{{u"create"_s, true}, {u"target"_s, u"main"_s}, {u"title"_s, u"Speed up"_s},
                                  {u"state"_s, u"wip"_s}, {u"reviewers"_s, u"alice, bob"_s},
                                  {u"labels"_s, u"perf, needs review"_s}};
        QVERIFY(m_repo->pushPreview(options).value(u"text"_s).toString().contains(
            "\ngh pr create --head topic --base main --title 'Speed up' --body '' --draft"_L1));
        QFile::remove(args);
        QSignalSpy notices(m_repo.get(), &Repository::notice);
        m_repo->push(false, options);
        QVERIFY(settle());
        QVERIFY(m_errors->isEmpty());
        QCOMPARE(remoteRev(u"refs/heads/topic"_s), rev(m_work, u"HEAD"_s));
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().at(1).toStringList(), QStringList{u"https://github.com/acme/widget/pull/13"_s});
        asked = readAll(args);
        QVERIFY2(asked.startsWith("pr\ncreate\n--repo\ngithub.com/acme/widget\n--head\ntopic\n--base\nmain\n"
                                  "--title\nSpeed up\n--body\n\n--draft\n--reviewer\nalice,bob\n"
                                  "--label\nperf,needs review\nprompt=1\n--\n"),
                 asked.constData());

        // gh signed out: the summary says so, pushing is left to the user.
        qputenv("FAKE_GH_FAIL", "To get started with GitHub CLI, please run:  gh auth login");
        m_repo->refresh();
        QTRY_VERIFY(m_repo->reviewSummary().contains("gh auth login"_L1));
        QVERIFY(m_repo->branchReview().isEmpty());
        QVERIFY(m_repo->pushNeeded());
        qunsetenv("FAKE_GH_FAIL");

        // gh not installed.
        QTemporaryDir onlyGit;
        QVERIFY(QFile::link(QStandardPaths::findExecutable(u"git"_s), onlyGit.filePath(u"git"_s)));
        qputenv("PATH", onlyGit.path().toUtf8());
        m_repo->refresh();
        QTRY_VERIFY(m_repo->reviewSummary().contains("not installed"_L1));
        QVERIFY(m_repo->reviewSummary().contains("gh auth login"_L1));
        qputenv("PATH", bin.path().toUtf8() + ':' + path);
    }

    void showsTheBranchMergeRequestFromGitLab()
    {
        // A stand-in for glab answering `glab api --hostname <host> <endpoint>`.
        QTemporaryDir bin;
        const QString args = bin.filePath(u"args"_s);
        const auto answer = [&](const char *name, const QByteArray &json) {
            QFile file(bin.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(json);
        };
        const QByteArray dir = bin.path().toUtf8();
        writeScript(bin.filePath(u"glab"_s),
                    "#!/bin/sh\n"
                    "printf '%s\\n' \"$@\" \"prompt=$NO_PROMPT\" -- >> \"" + args.toUtf8() + "\"\n"
                    "case \"$4\" in\n"
                    "*/approvals) cat \"" + dir + "/approvals.json\";;\n"
                    "*/discussions*) cat \"" + dir + "/discussions.json\";;\n"
                    "*merge_requests\\?*) cat \"" + dir + "/list.json\";;\n"
                    "*) cat \"" + dir + "/mr.json\";;\n"
                    "esac\n");
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", bin.path().toUtf8() + ':' + path);
        // Nothing may ask the real one once the stand-in is gone.
        const auto restore = qScopeGuard([this, path] {
            m_repo->setPushMode(u"plain"_s);
            QVERIFY(settle());
            qputenv("PATH", path);
        });

        // A self-hosted GitLab by a name igitt only knows from the setting.
        const QString url = u"git@git.example.org:group/sub/widget.git"_s;
        work({"config", "url." + m_remote + ".insteadOf", url});
        work({"remote", "set-url", "origin", url});
        work({"checkout", "-q", "-b", "topic"});
        commitIn(m_work, "topic work");
        work({"push", "-q", "-u", "origin", "topic"});
        answer("list.json", "[{'iid':7,'state':'opened','title':'Speed up','draft':true,'target_branch':'main',"
                                 "'web_url':'https://git.example.org/group/sub/widget/-/merge_requests/7'}]"_json);
        answer("mr.json", "{'iid':7,'state':'opened','title':'Speed up','draft':true,'target_branch':'main',"
                              "'sha':'"_json + rev(m_work, u"HEAD"_s).toUtf8() + "',"
                              "'web_url':'https://git.example.org/group/sub/widget/-/merge_requests/7',"
                              "'head_pipeline':{'status':'failed'}}"_json);
        answer("approvals.json", "{'approvals_left':1,'approved_by':[]}"_json);
        answer("discussions.json", "[{'notes':[{'resolvable':true,'resolved':false}]},"
                                       "{'notes':[{'resolvable':true,'resolved':false}]}]"_json);

        m_repo->setGitlabHosts(u"git.example.org"_s);
        open();
        QCOMPARE(m_repo->reviewHost(), u"gitlab"_s);
        QTRY_VERIFY(!m_repo->branchReview().isEmpty());
        const QVariantMap review = m_repo->commits()->data(m_repo->commits()->index(0), CommitModel::ReviewRole).toMap();
        QCOMPARE(review.value(u"kind"_s).toString(), u"mergeRequest"_s);
        QCOMPARE(review.value(u"label"_s).toString(), u"MR !7"_s);
        QCOMPARE(review.value(u"badges"_s).toString(), u"draft · 1 approval(s) left · pipeline failed · 2 unresolved"_s);
        QCOMPARE(review.value(u"checks"_s).toString(), u"failure"_s);
        QVERIFY(!review.value(u"details"_s).toString().contains("not this commit"_L1));
        QVERIFY(m_repo->reviewSummary().startsWith("Merge request !7: open, draft"_L1));
        const QByteArray asked = readAll(args);
        QVERIFY2(asked.startsWith("api\n--hostname\ngit.example.org\n"
                                  "projects/group%2Fsub%2Fwidget/merge_requests?source_branch=topic&state=all"),
                 asked.constData());
        QVERIFY(asked.contains("\nprojects/group%2Fsub%2Fwidget/merge_requests/7\nprompt=1\n"));
        QVERIFY(asked.contains("\nprojects/group%2Fsub%2Fwidget/merge_requests/7/approvals\n"));
        QVERIFY(asked.contains("\nprojects/group%2Fsub%2Fwidget/merge_requests/7/discussions?per_page=100\n"));

        // No merge request for the branch yet.
        answer("list.json", "[]");
        m_repo->refresh();
        QTRY_VERIFY(m_repo->reviewSummary().contains("No merge request for topic"_L1));
        QVERIFY(m_repo->branchReview().isEmpty());

        // Push options, as GitLab gets them; the "server" records them and
        // links the merge request, as GitLab does.
        GitRunner(m_root->path()).run({"--git-dir=" + m_remote, "config", "receive.advertisePushOptions", "true"});
        const QString received = m_root->filePath(u"push-options"_s);
        writeScript(m_remote + u"/hooks/post-receive"_s,
                    "#!/bin/sh\n"
                    "i=0\n"
                    "while [ $i -lt \"${GIT_PUSH_OPTION_COUNT:-0}\" ]; do\n"
                    "  eval \"echo \\\"\\$GIT_PUSH_OPTION_$i\\\"\"; i=$((i + 1))\n"
                    "done > \"" + received.toUtf8() + "\"\n"
                    "echo 'View it on GitLab:'\n"
                    "echo '  https://git.example.org/group/sub/widget/-/merge_requests/8'\n");
        commitIn(m_work, "more");
        m_repo->refresh();
        QVERIFY(settle());
        const QVariantMap options{{u"create"_s, true}, {u"target"_s, u"main"_s}, {u"title"_s, u"Speed up"_s},
                                  {u"state"_s, u"wip"_s}, {u"labels"_s, u"perf, needs review"_s},
                                  {u"removeSourceBranch"_s, true}};
        QCOMPARE(m_repo->pushPreview(options).value(u"text"_s).toString(),
                 u"git push -o merge_request.create -o merge_request.target=main -o 'merge_request.title=Speed up' "
                 "-o merge_request.draft -o merge_request.label=perf -o 'merge_request.label=needs review' "
                 "-o merge_request.remove_source_branch origin HEAD:refs/heads/topic"_s);
        QVERIFY(m_repo->pushPreview({{u"title"_s, u"a\nb"_s}}).value(u"error"_s).toString().contains("line break"_L1));
        QSignalSpy notices(m_repo.get(), &Repository::notice);
        m_repo->push(false, options);
        QVERIFY(settle());
        QVERIFY(m_errors->isEmpty());
        QCOMPARE(remoteRev(u"refs/heads/topic"_s), rev(m_work, u"HEAD"_s));
        QCOMPARE(readAll(received), "merge_request.create\nmerge_request.target=main\nmerge_request.title=Speed up\n"
                                    "merge_request.draft\nmerge_request.label=perf\nmerge_request.label=needs review\n"
                                    "merge_request.remove_source_branch\n"_ba);
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().at(1).toStringList(),
                 QStringList{u"https://git.example.org/group/sub/widget/-/merge_requests/8"_s});
    }

    void explainsWhatItCannotDo()
    {
        QVERIFY(explainNetworkFailure(u"fatal: could not read Username for 'https://x': terminal prompts disabled"_s)
                    .contains("credential helper"_L1));
        QVERIFY(explainNetworkFailure(u"Host key verification failed."_s).contains("terminal"_L1));
        QCOMPARE(remoteLinks(u"remote: Processing changes: new: 1\nremote:   https://review.example/c/1 [NEW]\n"_s),
                 QStringList{u"https://review.example/c/1"_s});
    }

private:
    void open()
    {
        m_repo->open(m_work.workDir());
        QVERIFY(settle());
        QVERIFY(m_repo->valid());
    }

    [[nodiscard]] bool settle()
    {
        return QTest::qWaitFor([this] { return !m_repo->busy(); }, 20000);
    }

    QByteArray work(const QStringList &args) { return m_work.run(args).out; }

    static void writeScript(const QString &path, const QByteArray &script)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(script);
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    }

    static QByteArray readAll(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    static QString rev(const GitRunner &git, const QString &r)
    {
        return QString::fromLatin1(git.run({"rev-parse", r}).out.trimmed());
    }

    QString remoteRev(const QString &ref)
    {
        return QString::fromLatin1(GitRunner(m_root->path()).run({"--git-dir=" + m_remote, "rev-parse", ref}).out.trimmed());
    }

    void commitWithChangeId(const QString &subject, const QString &changeId)
    {
        commitIn(m_work, subject + u"\n\nChange-Id: "_s + changeId);
    }

    void commitIn(const GitRunner &git, const QString &message)
    {
        QFile file(git.workDir() + u"/file.txt"_s);
        QVERIFY(file.open(QIODevice::Append));
        file.write(message.toUtf8() + '\n');
        file.close();
        git.run({"add", "file.txt"});
        git.run({"commit", "-q", "-m", message});
    }

    std::unique_ptr<QTemporaryDir> m_root;
    QString m_remote;
    GitRunner m_work;
    GitRunner m_other;
    std::unique_ptr<Repository> m_repo;
    std::unique_ptr<QSignalSpy> m_errors;
};

QTEST_GUILESS_MAIN(TestRemote)
#include "tst_remote.moc"
