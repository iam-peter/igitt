#include "core/FileHistory.h"
#include "models/Timelapse.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestTimelapse : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_COMMITTER_NAME", "Test");
        qputenv("GIT_COMMITTER_EMAIL", "test@example.com");
        QVERIFY(m_dir.isValid());
        m_git.setWorkDir(m_dir.path());
        git({"init", "-q", "-b", "main"});

        // r0 creates the file, r1 changes a line and adds one, r2 renames
        // it and deletes the first line.
        write("f.txt", "alpha\nbeta\ngamma\ndelta\nepsilon\n");
        commit("Create", "Ann");
        write("f.txt", "alpha\nBETA\ngamma\ndelta\nepsilon\nzeta\n");
        commit("Change beta", "Bob");
        QFile::remove(m_dir.filePath("f.txt"));
        write("g.txt", "BETA\ngamma\ndelta\nepsilon\nzeta\n");
        commit("Rename and drop alpha", "Cid");
    }

    void followsRenames()
    {
        const QList<FileRevision> revisions = loadFileRevisions(m_git, u"g.txt"_s);
        QCOMPARE(revisions.size(), 3);
        QCOMPARE(revisions[0].subject, u"Create"_s);
        QCOMPARE(revisions[0].path, u"f.txt"_s);
        QCOMPARE(revisions[2].path, u"g.txt"_s);
        QCOMPARE(revisions[1].author, u"Bob"_s);
    }

    void blamesEachLineToItsRevision()
    {
        const QList<FileRevision> revisions = loadFileRevisions(m_git, u"g.txt"_s);
        const QList<TimelapseLine> lines = loadTimelapse(m_git, revisions, 1, false);
        QCOMPARE(lines.size(), 6);
        QCOMPARE(lines[0].text, u"alpha"_s);
        QCOMPARE(lines[0].revision, 0);
        QVERIFY(!lines[0].changedHere);
        QCOMPARE(lines[1].text, u"BETA"_s);
        QCOMPARE(lines[1].revision, 1);
        QCOMPARE(lines[1].author, u"Bob"_s);
        QVERIFY(lines[1].changedHere);
        QVERIFY(lines[5].changedHere); // zeta
    }

    void showsDeletedLinesWhereTheyWere()
    {
        const QList<FileRevision> revisions = loadFileRevisions(m_git, u"g.txt"_s);
        QList<TimelapseLine> lines = loadTimelapse(m_git, revisions, 1, true);
        QCOMPARE(lines.size(), 7);
        QCOMPARE(lines[1].kind, TimelapseLine::Deleted);
        QCOMPARE(lines[1].text, u"beta"_s);
        QCOMPARE(lines[2].text, u"BETA"_s);

        // Across the rename too; nothing in it was changed, only removed.
        lines = loadTimelapse(m_git, revisions, 2, true);
        QCOMPARE(lines.first().kind, TimelapseLine::Deleted);
        QCOMPARE(lines.first().text, u"alpha"_s);
        QCOMPARE(lines[1].revision, 1);
        QVERIFY(std::none_of(lines.cbegin() + 1, lines.cend(), [](const TimelapseLine &l) { return l.changedHere; }));
    }

    void modelLoadsInTheBackground()
    {
        Timelapse lapse;
        lapse.setWorkDir(m_dir.path());
        lapse.setPath(u"g.txt"_s);
        QTRY_COMPARE(lapse.revisionCount(), 3);
        QCOMPARE(lapse.revision(), 2); // the newest
        QTRY_VERIFY(!lapse.busy() && lapse.rowCount() == 6); // alpha deleted, 5 lines

        lapse.setRevision(0);
        QTRY_VERIFY(!lapse.busy() && lapse.rowCount() == 5);
        QCOMPARE(lapse.data(lapse.index(0), Timelapse::AgeRole).toReal(), 0.0);
        QVERIFY(lapse.data(lapse.index(0), Timelapse::ChangedHereRole).toBool());

        lapse.setPath(u"missing.txt"_s);
        QTRY_VERIFY(!lapse.error().isEmpty());
        QCOMPARE(lapse.revisionCount(), 0);
    }

private:
    QByteArray git(const QStringList &args) { return m_git.run(args).out; }

    void write(const QString &name, const QByteArray &content)
    {
        QFile file(m_dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    void commit(const QString &message, const QString &author)
    {
        QProcessEnvironment env;
        env.insert(u"GIT_AUTHOR_NAME"_s, author);
        env.insert(u"GIT_AUTHOR_EMAIL"_s, author.toLower() + u"@example.com"_s);
        git({"add", "-A"});
        m_git.run({"commit", "-q", "-m", message}, {}, env);
    }

    QTemporaryDir m_dir;
    GitRunner m_git;
};

QTEST_GUILESS_MAIN(TestTimelapse)
#include "tst_timelapse.moc"
