#include "Repository.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

} // namespace

class TestRepository : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keep the user's configuration (signing, hooks, aliases) out of it.
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
        writeFile(file(), "one\ntwo\nthree\nfour\nfive\n");
        git({"add", "f.txt"});
        git({"commit", "-q", "-m", "First"});
        writeFile(m_dir->filePath("g.txt"), "g\n");
        git({"add", "g.txt"});
        git({"commit", "-q", "-m", "Second"});
    }

    void opensHistoryAndStatus()
    {
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        writeFile(m_dir->filePath("new.txt"), "new\n");

        Repository repo;
        QVERIFY(repo.open(m_dir->filePath("f.txt").section('/', 0, -2)));
        QCOMPARE(repo.branch(), u"main"_s);
        QCOMPARE(repo.commits()->rowCount(), 2);
        QCOMPARE(repo.commits()->data(repo.commits()->index(0), CommitModel::SubjectRole).toString(),
                 u"Second"_s);
        QCOMPARE(repo.unstaged()->count(), 2);
        QCOMPARE(repo.staged()->count(), 0);
    }

    void rejectsNonRepository()
    {
        QTemporaryDir plain;
        Repository repo;
        QSignalSpy errors(&repo, &Repository::errorOccurred);
        QVERIFY(!repo.open(plain.path()));
        QCOMPARE(errors.size(), 1);
        QVERIFY(!repo.valid());
    }

    void stagesSelectedLines()
    {
        writeFile(file(), "one\nTWO\nthree\nFOUR\nfive\n");
        Repository repo;
        QVERIFY(repo.open(m_dir->path()));

        repo.showFile(u"f.txt"_s, false);
        selectLinesContaining(repo, {u"two"_s, u"TWO"_s});
        QCOMPARE(repo.diff()->selectedCount(), 2);
        QVERIFY(repo.applySelection());

        QVERIFY(git({"diff", "--cached"}).contains("+TWO"));
        QVERIFY(!git({"diff", "--cached"}).contains("FOUR"));
        QVERIFY(git({"diff"}).contains("+FOUR"));
        QVERIFY(!git({"diff"}).contains("+TWO"));
        QCOMPARE(repo.staged()->count(), 1);
    }

    void unstagesSelectedLines()
    {
        writeFile(file(), "one\nTWO\nthree\nFOUR\nfive\n");
        git({"add", "f.txt"});
        Repository repo;
        QVERIFY(repo.open(m_dir->path()));

        repo.showFile(u"f.txt"_s, true);
        selectLinesContaining(repo, {u"four"_s, u"FOUR"_s});
        QVERIFY(repo.applySelection());

        QVERIFY(git({"diff", "--cached"}).contains("+TWO"));
        QVERIFY(!git({"diff", "--cached"}).contains("FOUR"));
        QVERIFY(git({"diff"}).contains("+FOUR"));
    }

    void stagesPartOfUntrackedFile()
    {
        writeFile(m_dir->filePath("new.txt"), "keep\nlater\n");
        Repository repo;
        QVERIFY(repo.open(m_dir->path()));

        repo.showFile(u"new.txt"_s, false);
        selectLinesContaining(repo, {u"keep"_s});
        QVERIFY(repo.applySelection());
        QCOMPARE(git({"show", ":new.txt"}), "keep\n"_ba);
    }

    void stagesPartOfDeletion()
    {
        QFile::remove(file());
        Repository repo;
        QVERIFY(repo.open(m_dir->path()));

        repo.showFile(u"f.txt"_s, false);
        selectLinesContaining(repo, {u"one"_s});
        QVERIFY(repo.applySelection());
        QCOMPARE(git({"show", ":f.txt"}), "two\nthree\nfour\nfive\n"_ba);
    }

    void commitsAndAmends()
    {
        writeFile(file(), "one\nTWO\nthree\nfour\nfive\n");
        Repository repo;
        QVERIFY(repo.open(m_dir->path()));
        QVERIFY(repo.stageFile(u"f.txt"_s));
        QVERIFY(repo.commit(u"Third"_s, false));
        QCOMPARE(repo.commits()->rowCount(), 3);
        QCOMPARE(repo.headMessage(), u"Third"_s);

        QVERIFY(repo.commit(u"Third, reworded"_s, true));
        QCOMPARE(repo.commits()->rowCount(), 3);
        QCOMPARE(repo.headMessage(), u"Third, reworded"_s);
        QCOMPARE(repo.staged()->count(), 0);
    }

private:
    QString file() const { return m_dir->filePath("f.txt"); }

    QByteArray git(const QStringList &args)
    {
        const GitResult result = m_git.run(args);
        if (!result.ok())
            qWarning() << args << result.errorText();
        return result.out;
    }

    // Selects every changed line whose text is one of `texts`.
    static void selectLinesContaining(Repository &repo, const QStringList &texts)
    {
        DiffModel *diff = repo.diff();
        for (int row = 0; row < diff->rowCount(); ++row) {
            const QModelIndex idx = diff->index(row);
            if (diff->data(idx, DiffModel::SelectableRole).toBool()
                && texts.contains(diff->data(idx, DiffModel::TextRole).toString()))
                diff->toggleLine(row);
        }
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    GitRunner m_git;
};

QTEST_GUILESS_MAIN(TestRepository)
#include "tst_repository.moc"
