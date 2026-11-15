#include "core/AskPass.h"
#include "core/GitRunner.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <thread>

using namespace Qt::StringLiterals;

// git asks through the helper, which is this test itself, as it is igitt
// in the app
class TestAskPass : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        QVERIFY(AskPass::instance()->start(QCoreApplication::applicationFilePath()));
    }

    void knowsSecrets()
    {
        QVERIFY(AskPass::isSecret(u"Password for 'https://alice@example.com': "_s));
        QVERIFY(AskPass::isSecret(u"Enter passphrase for key '/home/a/.ssh/id_ed25519': "_s));
        QVERIFY(!AskPass::isSecret(u"Username for 'https://example.com': "_s));
        QVERIFY(!AskPass::isSecret(u"Are you sure you want to continue connecting (yes/no/[fingerprint])? "_s));
    }

    void answersGit()
    {
        QStringList prompts;
        QList<bool> secrets;
        auto *askPass = AskPass::instance();
        const auto connection = connect(askPass, &AskPass::promptRequested, this,
                                        [&](int id, const QString &prompt, bool secret) {
            prompts << prompt;
            secrets << secret;
            askPass->answer(id, secret ? u"s3cret ünïcode"_s : u"alice"_s);
        });
        const GitResult result = fill();
        disconnect(connection);
        QVERIFY2(result.ok(), result.err.constData());
        QVERIFY(result.out.contains("username=alice\n"));
        QVERIFY(result.out.contains("password=s3cret ünïcode\n"));
        QCOMPARE(prompts.size(), 2);
        QVERIFY(prompts.first().startsWith(u"Username"_s));
        QCOMPARE(secrets, (QList<bool>{false, true}));
    }

    void cancelStopsGit()
    {
        auto *askPass = AskPass::instance();
        const auto connection = connect(askPass, &AskPass::promptRequested, this,
                                        [askPass](int id) { askPass->cancel(id); });
        const GitResult result = fill();
        disconnect(connection);
        QVERIFY(!result.ok());
        QVERIFY(!result.out.contains("password="));
    }

private:
    // What git would send a server, asked of the user
    GitResult fill()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        GitResult result;
        std::atomic<bool> done = false;
        std::thread thread([&] {
            result = git.run({u"credential"_s, u"fill"_s}, "protocol=https\nhost=example.com\n\n");
            done = true;
        });
        const bool finished = QTest::qWaitFor([&] { return done.load(); }, 20000);
        thread.join();
        if (!finished)
            result.err = "git did not finish";
        return result;
    }
};

int main(int argc, char *argv[])
{
    if (AskPass::isHelperCall(argc)) {
        QCoreApplication helper(argc, argv);
        return AskPass::runHelper(QString::fromLocal8Bit(argv[1]));
    }
    QCoreApplication app(argc, argv);
    TestAskPass test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_askpass.moc"
