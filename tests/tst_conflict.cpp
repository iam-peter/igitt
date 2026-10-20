#include "Repository.h"
#include "core/FoldEngine.h"
#include "core/MergeConflict.h"
#include "core/PlanSession.h"
#include "models/ConflictResolver.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestConflict : public QObject
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

    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_git.setWorkDir(m_dir->path());
        git({"init", "-q", "-b", "main"});
        write("f.txt", "one\ntwo\nthree\nfour\nfive\nsix\nseven\n");
        write("gone.txt", "gone\n");
        commit("Base");
        git({"checkout", "-q", "-b", "side"});
        write("f.txt", "one\nTWO (side)\nthree\nfour\nfive\nSIX (side)\nseven\n");
        write("gone.txt", "changed on side\n");
        commit("Side");
        git({"checkout", "-q", "main"});
        write("f.txt", "one\nTWO (main)\nthree\nfour\nfive\nSIX (main)\nseven\n");
        QFile::remove(m_dir->filePath("gone.txt"));
        commit("Main");
        git({"merge", "-q", "side"}); // conflicts
    }

    void parsesConflicts()
    {
        const ConflictFile file = loadConflict(m_git, u"f.txt"_s);
        QVERIFY2(file.error.isEmpty(), qPrintable(file.error));
        QCOMPARE(file.operation, u"Merging"_s);
        QVERIFY(file.oursLabel.contains("Main"_L1));
        QVERIFY(file.theirsLabel.contains("Side"_L1));

        QCOMPARE(file.chunks.size(), 5);
        QCOMPARE(file.chunks[0].lines, QStringList{u"one"_s});
        QVERIFY(file.chunks[1].conflict);
        QCOMPARE(file.chunks[1].ours, QStringList{u"TWO (main)"_s});
        QCOMPARE(file.chunks[1].base, QStringList{u"two"_s});
        QCOMPARE(file.chunks[1].theirs, QStringList{u"TWO (side)"_s});
        QCOMPARE(file.chunks[2].lines, (QStringList{u"three"_s, u"four"_s, u"five"_s}));
        QVERIFY(file.chunks[3].conflict);
    }

    void composesTheOutput()
    {
        const ConflictFile file = loadConflict(m_git, u"f.txt"_s);
        QCOMPARE(resolveConflict(file, {{true, false, false}, {false, true, false}}),
                 u"one\nTWO (main)\nthree\nfour\nfive\nSIX (side)\nseven\n"_s);
        // Both, in the order picked; nothing picked keeps the markers.
        QCOMPARE(resolveConflict(file, {{true, true, true}, {}}),
                 u"one\nTWO (side)\nTWO (main)\nthree\nfour\nfive\n<<<<<<< ours\nSIX (main)\n=======\n"
                 "SIX (side)\n>>>>>>> theirs\nseven\n"_s);
    }

    void resolverTracksChoices()
    {
        ConflictResolver resolver;
        resolver.setWorkDir(m_dir->path());
        resolver.setPath(u"f.txt"_s);
        QTRY_VERIFY(!resolver.busy());
        QCOMPARE(resolver.conflictCount(), 2);
        QCOMPARE(resolver.unresolvedCount(), 2);

        resolver.choose(0, true, true);  // theirs first ...
        resolver.choose(0, false, true); // ... then ours
        resolver.choose(1, false, true);
        QCOMPARE(resolver.unresolvedCount(), 0);
        QCOMPARE(resolver.output(), u"one\nTWO (side)\nTWO (main)\nthree\nfour\nfive\nSIX (main)\nseven\n"_s);

        resolver.takeAll(true);
        QCOMPARE(resolver.output(), u"one\nTWO (side)\nthree\nfour\nfive\nSIX (side)\nseven\n"_s);
    }

    void savesTheResolution()
    {
        Repository repo;
        repo.open(m_dir->path());
        QVERIFY(settle(repo));
        repo.showFile(u"f.txt"_s, false);
        QVERIFY(settle(repo));
        QVERIFY(repo.diffConflicted());

        repo.resolveConflict(u"f.txt"_s, u"resolved\n"_s);
        QVERIFY(settle(repo));
        QCOMPARE(read("f.txt"), "resolved\n"_ba);
        QVERIFY(!git({"ls-files", "-u", "--", "f.txt"}).size());
        // Staged as stage 0, i.e. resolved.
        QVERIFY(git({"ls-files", "-s", "--", "f.txt"}).contains(" 0\tf.txt"));
    }

    void foldAndPlanWaitForTheMerge()
    {
        QVERIFY(FoldEngine(m_git).blocker(u"HEAD"_s).contains("Merging"_L1));
        PlanSession plan;
        QString error;
        QVERIFY(!plan.begin(m_git, error));
        QVERIFY(error.contains("Merging"_L1));
    }

    void resolvesDeletionsBySide()
    {
        const ConflictFile file = loadConflict(m_git, u"gone.txt"_s);
        QVERIFY(file.deletedInOurs);
        QVERIFY(!file.error.isEmpty());

        Repository repo;
        repo.open(m_dir->path());
        QVERIFY(settle(repo));
        repo.resolveWithSide(u"gone.txt"_s, true);
        QVERIFY(settle(repo));
        QCOMPARE(read("gone.txt"), "changed on side\n"_ba);
        QVERIFY(git({"ls-files", "-u", "--", "gone.txt"}).isEmpty());

        // Starting over, keep ours: the file goes.
        init();
        Repository again;
        again.open(m_dir->path());
        QVERIFY(settle(again));
        again.resolveWithSide(u"gone.txt"_s, false);
        QVERIFY(settle(again));
        QVERIFY(!QFile::exists(m_dir->filePath("gone.txt")));
        QVERIFY(git({"ls-files", "--", "gone.txt"}).isEmpty());
    }

private:
    [[nodiscard]] static bool settle(Repository &repo)
    {
        return QTest::qWaitFor([&repo] { return !repo.busy(); }, 10000);
    }

    QByteArray git(const QStringList &args) { return m_git.run(args).out; }

    void write(const QString &name, const QByteArray &content)
    {
        QFile file(m_dir->filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    QByteArray read(const QString &name)
    {
        QFile file(m_dir->filePath(name));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    void commit(const QString &message)
    {
        git({"add", "-A"});
        git({"commit", "-q", "-m", message});
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    GitRunner m_git;
};

QTEST_GUILESS_MAIN(TestConflict)
#include "tst_conflict.moc"
