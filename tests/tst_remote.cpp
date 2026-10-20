#include "Repository.h"
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
