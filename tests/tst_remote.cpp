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
    }

    void asksGerritWhetherThereIsAnythingToPush()
    {
        // A stand-in for ssh that answers like Gerrit.
        QTemporaryDir bin;
        QFile ssh(bin.filePath(u"ssh"_s));
        QVERIFY(ssh.open(QIODevice::WriteOnly));
        ssh.write("#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$FAKE_SSH_ARGS\"\n"
                  "[ -n \"$FAKE_SSH_FAIL\" ] && { echo 'Connection refused' >&2; exit 255; }\n"
                  "cat \"$FAKE_GERRIT_JSON\"\n");
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
            const auto line = [](const char *id, int number, const QString &revision) {
                return QStringLiteral(R"({"id":"%1","number":%2,"url":"https://review.example/c/proj/+/%2",)"
                                      R"("status":"NEW","currentPatchSet":{"number":2,"revision":"%3"}})")
                    .arg(QLatin1StringView(id)).arg(number).arg(revision).toUtf8() + '\n';
            };
            file.write(line("I1111111111111111111111111111111111111111", 101, rev(m_work, u"HEAD~1"_s)));
            file.write(line("I2222222222222222222222222222222222222222", 102, secondRevision));
            file.write(R"({"type":"stats","rowCount":2})" "\n");
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
