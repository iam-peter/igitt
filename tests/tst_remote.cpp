#include "Repository.h"
#include "core/GerritReview.h"
#include "core/RemoteOps.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

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
        QCOMPARE(voted.comments.size(), 2);

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

        // Messages and patch sets, from --comments and --patch-sets.
        const GerritChange history = parseGerritQuery(
            R"({"id":"I3","number":5,"status":"NEW","currentPatchSet":{"number":2,"revision":"r2"},)"
            R"("comments":[{"timestamp":1760000000,"reviewer":{"name":"Alice"},"message":"Patch Set 1: Code-Review-1"}],)"
            R"("patchSets":[{"number":2,"revision":"r2","uploader":{"username":"me"},"createdOn":1760003600,)"
            R"("kind":"TRIVIAL_REBASE"},)"
            R"({"number":1,"revision":"r1","ref":"refs/changes/05/5/1","kind":"REWORK",)"
            R"("comments":[{"file":"a.cpp","line":4,"reviewer":{"name":"Alice"},"message":"Why?"},)"
            R"({"file":"a.cpp","reviewer":{"name":"Alice"},"message":"On the file"}]}]})"_ba)
            .value(u"I3"_s);
        QCOMPARE(history.messages.size(), 1);
        QCOMPARE(history.messages[0].author, u"Alice"_s);
        QCOMPARE(history.messages[0].date, QDateTime::fromSecsSinceEpoch(1760000000));
        QCOMPARE(history.patchSets.size(), 2);
        QCOMPARE(history.patchSets[0].number, 1); // sorted
        QCOMPARE(history.patchSets[0].comments.size(), 2);
        QCOMPARE(history.patchSets[0].comments[0].line, 4);
        QCOMPARE(history.patchSets[0].comments[0].patchSet, 1);
        QCOMPARE(history.patchSets[0].comments[1].line, 0);
        QCOMPARE(history.patchSets[1].uploader, u"me"_s);
        QCOMPARE(history.patchSets[1].created, QDateTime::fromSecsSinceEpoch(1760003600));
        QCOMPARE(history.patchSets[1].ref, u"refs/changes/05/5/2"_s); // worked out when missing
        QCOMPARE(patchSetKindText(history.patchSets[1].kind), u"Trivial rebase"_s);
        QCOMPARE(patchSetKindText(u"SOMETHING_NEW"_s), u"SOMETHING_NEW"_s);
        QCOMPARE(patchSetRef(123456, 7), u"refs/changes/56/123456/7"_s);
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
        QCOMPARE(m_repo->reviewRefspec(options).value(u"refspec"_s).toString(), u"HEAD:"_s + refspec);

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
        QVERIFY(m_repo->reviewRefspec(bad).value(u"error"_s).toString().contains("two words"_L1));
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

        // Gerrit's URL for queries, the bare repository for git.
        work({"remote", "set-url", "origin", "ssh://tester@review.example:29418/proj/sub"});
        work({"config", "url." + m_remote + ".insteadOf", "ssh://tester@review.example:29418/proj/sub"});
        commitWithChangeId("first", "I1111111111111111111111111111111111111111");
        // Patch set 1 of the second change, uploaded from elsewhere: only
        // the remote has it, under refs/changes/.
        work({"push", "-q", "origin", "HEAD:refs/changes/01/101/2"});
        m_other.run({"fetch", "-q", "origin", "refs/changes/01/101/2"});
        m_other.run({"checkout", "-q", "FETCH_HEAD"});
        commitIn(m_other, "second, first try");
        m_other.run({"push", "-q", "origin", "HEAD:refs/changes/02/102/1"});
        const QString firstTry = rev(m_other, u"HEAD"_s);
        commitWithChangeId("second", "I2222222222222222222222222222222222222222");
        const auto answer = [&](const QString &secondRevision) {
            QFile file(json);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            const auto line = [](const char *id, int number, const QString &revision, const QString &extra,
                                 const char *approvals, const char *comments) {
                return QStringLiteral(R"({"id":"%1","number":%2,"url":"https://review.example/c/proj/+/%2",)"
                                      R"("status":"NEW",%4"currentPatchSet":{"number":2,"revision":"%3",)"
                                      R"("approvals":[%5],"comments":[%6]}})")
                    .arg(QLatin1StringView(id)).arg(number).arg(revision, extra, QLatin1StringView(approvals),
                                                                 QLatin1StringView(comments))
                    .toUtf8() + '\n';
            };
            const char *secondComments = R"({"file":"file.txt","line":5,"reviewer":{"name":"Alice"},"message":"Say why."})";
            file.write(line("I1111111111111111111111111111111111111111", 101, rev(m_work, u"HEAD~1"_s), {},
                            R"({"type":"Code-Review","value":"2","by":{"name":"Alice"}},)"
                            R"({"type":"Verified","value":"1","by":{"name":"Qt CI Bot"}})",
                            R"({"file":"file.txt","line":1})"));
            const QString history =
                QStringLiteral(
                    R"("comments":[{"timestamp":1760000000,"reviewer":{"name":"Tester"},"message":"Uploaded patch set 1."},)"
                    R"json({"timestamp":1760003600,"reviewer":{"name":"Alice"},"message":"Patch Set 1: Code-Review-1\n\n(1 comment)"},)json"
                    R"({"timestamp":1760007200,"reviewer":{"name":"Tester"},"message":"Uploaded patch set 2."}],)"
                    R"("patchSets":[{"number":1,"revision":"%1","ref":"refs/changes/02/102/1","uploader":{"name":"Tester"},)"
                    R"("createdOn":1760000000,"kind":"REWORK","comments":[)"
                    R"({"file":"file.txt","line":5,"reviewer":{"username":"bob"},"message":"Typo?"},)"
                    R"({"file":"file.txt","reviewer":{"name":"Bob"},"message":"Split this file?"}]},)"
                    R"({"number":2,"revision":"%2","ref":"refs/changes/02/102/2","uploader":{"name":"Tester"},)"
                    R"("createdOn":1760007200,"kind":"REWORK","comments":[%3]}],)")
                    .arg(firstTry, secondRevision, QLatin1StringView(secondComments));
            file.write(line("I2222222222222222222222222222222222222222", 102, secondRevision,
                            uR"("wip":true,"topic":"speedup",)"_s + history,
                            R"({"type":"Code-Review","value":"1","by":{"name":"Alice"}},)"
                            R"({"type":"Verified","value":"-1","by":{"name":"Qt CI Bot"}})",
                            secondComments));
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
        QVERIFY(query.contains("--current-patch-set\n--patch-sets\n--comments"));
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

        // The review of the commit on display: its messages, comments and
        // patch sets.
        const QString head = rev(m_work, u"HEAD"_s);
        m_repo->showCommit(head);
        QVERIFY(settle());
        const QVariantMap shown = m_repo->commitReview();
        QCOMPARE(shown.value(u"number"_s).toInt(), 102);
        QCOMPARE(shown.value(u"localPatchSet"_s).toInt(), 2);
        QCOMPARE(shown.value(u"unresolved"_s), QVariant(true));
        const QVariantList messages = shown.value(u"messages"_s).toList();
        QCOMPARE(messages.size(), 3);
        QCOMPARE(messages[1].toMap().value(u"author"_s).toString(), u"Alice"_s);
        QCOMPARE(messages[1].toMap().value(u"text"_s).toString(), u"Patch Set 1: Code-Review-1\n\n(1 comment)"_s);
        QCOMPARE(messages[1].toMap().value(u"date"_s).toDateTime(), QDateTime::fromSecsSinceEpoch(1760003600));
        const QVariantList patchSets = shown.value(u"patchSets"_s).toList();
        QCOMPARE(patchSets.size(), 2);
        QCOMPARE(patchSets[0].toMap().value(u"number"_s).toInt(), 2); // newest first
        QVERIFY(patchSets[0].toMap().value(u"local"_s).toBool());
        QVERIFY(patchSets[0].toMap().value(u"current"_s).toBool());
        QCOMPARE(patchSets[1].toMap().value(u"revision"_s).toString(), firstTry);
        QVERIFY(!patchSets[1].toMap().value(u"local"_s).toBool());
        QCOMPARE(patchSets[1].toMap().value(u"kindText"_s).toString(), u"Rework"_s);
        QCOMPARE(patchSets[1].toMap().value(u"uploader"_s).toString(), u"Tester"_s);
        QCOMPARE(patchSets[1].toMap().value(u"comments"_s).toInt(), 2);
        const auto comment = [&](int i) { return m_repo->commitReview().value(u"comments"_s).toList().value(i).toMap(); };
        QCOMPARE(shown.value(u"comments"_s).toList().size(), 3);
        QCOMPARE(comment(0).value(u"patchSet"_s).toInt(), 2);
        QCOMPARE(comment(0).value(u"file"_s).toString(), u"file.txt"_s);
        QCOMPARE(comment(0).value(u"line"_s).toInt(), 5);
        QCOMPARE(comment(0).value(u"text"_s).toString(), u"Say why."_s);
        QVERIFY(comment(0).value(u"marked"_s).toBool()); // this commit is patch set 2
        QCOMPARE(comment(1).value(u"author"_s).toString(), u"bob"_s);
        QVERIFY(!comment(1).value(u"marked"_s).toBool()); // patch set 1 is not in this diff
        QCOMPARE(comment(2).value(u"line"_s).toInt(), 0);
        QVERIFY(!comment(2).value(u"marked"_s).toBool());
        QVariantMap marks = m_repo->diffComments();
        QCOMPARE(marks.value(u"newSide"_s).toMap().value(u"file.txt:5"_s).toList().value(0).toMap()
                     .value(u"text"_s).toString(), u"Say why."_s);
        QVERIFY(marks.value(u"oldSide"_s).toMap().isEmpty());
        // Line 5 is what the commit added.
        QVERIFY(diffRows().contains(u"5 +second"_s));

        // Compared with patch set 1, which is fetched for it.
        QVERIFY(!m_work.run({"cat-file", "-e", firstTry}).ok());
        m_repo->comparePatchSet(1);
        QVERIFY(settle());
        QCOMPARE(m_repo->diffSource(), Repository::PatchSetDiff);
        QCOMPARE(m_repo->diffPatchSet(), 1);
        QCOMPARE(m_repo->diffCommit(), head);
        QVERIFY(m_work.run({"cat-file", "-e", firstTry}).ok());
        QVERIFY(work({"for-each-ref", "refs/changes"}).isEmpty()); // no ref kept
        QVERIFY(!m_repo->lastFetch().isValid());                   // nor FETCH_HEAD
        QVERIFY2(m_repo->commitDetails().contains("patch set 1"_L1), qPrintable(m_repo->commitDetails()));
        QVERIFY(!m_repo->commitDetails().contains("different parents"_L1));
        const QStringList rows = diffRows();
        QVERIFY2(rows.contains(u"5 -second, first try"_s), qPrintable(rows.join(u'\n')));
        QVERIFY(rows.contains(u"5 +second"_s));
        // Both sides are patch sets now, and show their comments.
        marks = m_repo->diffComments();
        QCOMPARE(marks.value(u"oldSide"_s).toMap().value(u"file.txt:5"_s).toList().value(0).toMap()
                     .value(u"text"_s).toString(), u"Typo?"_s);
        QCOMPARE(marks.value(u"newSide"_s).toMap().size(), 1);
        QVERIFY(comment(0).value(u"marked"_s).toBool());
        QVERIFY(comment(1).value(u"marked"_s).toBool());
        QVERIFY(!comment(2).value(u"marked"_s).toBool()); // on the file, not a line
        // Back to the commit on its own.
        m_repo->showCommit(head);
        QVERIFY(settle());
        QCOMPARE(m_repo->diffPatchSet(), 0);
        QVERIFY(m_repo->diffComments().value(u"oldSide"_s).toMap().isEmpty());

        // The newest commit changed since its upload: pushing makes sense again.
        answer(u"0123456789abcdef0123456789abcdef01234567"_s);
        m_repo->refresh();
        QTRY_VERIFY(m_repo->pushNeeded());
        QCOMPARE(m_repo->commits()->data(m_repo->commits()->index(0), CommitModel::ReviewRole).toMap()
                     .value(u"state"_s).toString(), u"outdated"_s);
        // No patch set is this commit, so no comment is on its lines.
        QCOMPARE(m_repo->commitReview().value(u"localPatchSet"_s).toInt(), 0);
        QVERIFY(m_repo->diffComments().value(u"newSide"_s).toMap().isEmpty());
        QVERIFY(!comment(0).value(u"marked"_s).toBool());
        // A patch set the remote does not have cannot be compared with.
        m_repo->comparePatchSet(2);
        QVERIFY(settle());
        QVERIFY2(diffRows().value(0).contains("Fetching refs/changes/02/102/2 from origin failed"_L1),
                 qPrintable(diffRows().value(0)));

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

    // The diff on display, a row each: "<new line> +text" for added
    // lines, "<old line> -text" for removed ones.
    QStringList diffRows() const
    {
        QStringList rows;
        const DiffModel *diff = m_repo->diff();
        for (int i = 0; i < diff->rowCount(); ++i) {
            const QModelIndex index = diff->index(i);
            const QString text = diff->data(index, DiffModel::TextRole).toString();
            switch (diff->data(index, DiffModel::KindRole).toInt()) {
            case DiffModel::Added:
                rows << u"%1 +%2"_s.arg(diff->data(index, DiffModel::NewLineRole).toInt()).arg(text);
                break;
            case DiffModel::Removed:
                rows << u"%1 -%2"_s.arg(diff->data(index, DiffModel::OldLineRole).toInt()).arg(text);
                break;
            default:
                rows << text;
            }
        }
        return rows;
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
