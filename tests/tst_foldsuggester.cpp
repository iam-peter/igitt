#include "core/DiffParser.h"
#include "core/FoldSuggester.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestFoldSuggester : public QObject
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
        m_changes.clear();
        git({"init", "-q", "-b", "main"});

        // "Upstream" owns all 20 lines; the local commits change some.
        write(lines({}));
        commit("Upstream");
        git({"remote", "add", "origin", m_dir->path()});
        git({"update-ref", "refs/remotes/origin/main", "HEAD"});
        git({"branch", "-q", "--set-upstream-to=origin/main"});

        m_changes[5] = "five (L1)";
        write(lines(m_changes));
        commit("L1");
        m_changes[10] = "ten (L2)";
        m_changes[11] = "eleven (L2)";
        write(lines(m_changes));
        commit("L2");
        m_changes[15] = "fifteen (L3)";
        write(lines(m_changes));
        commit("L3");
    }

    void modifiedLineBelongsToItsCommit()
    {
        QCOMPARE(suggestFor({{10, "ten, fixed"}}).target, rev("HEAD~1"));
    }

    void additionBelongsToTheNewestNeighbour()
    {
        // Inserted between line 5 (L1) and line 6 (upstream).
        const FoldSuggestion s = suggestFor({}, {{5, "five and a half"}});
        QCOMPARE(s.status, FoldSuggestion::Single);
        QCOMPARE(s.target, rev("HEAD~2"));
    }

    void adjacentLineOfNewerCommitWins()
    {
        // Line 12 is upstream code, but folding it below L2, which changed
        // line 11, would conflict.
        QCOMPARE(suggestFor({{12, "twelve, fixed"}}).target, rev("HEAD~1"));
    }

    void selectionSpanningCommitsIsAmbiguous()
    {
        const FoldSuggestion s = suggestFor({{5, "five, fixed"}, {15, "fifteen, fixed"}});
        QCOMPARE(s.status, FoldSuggestion::Ambiguous);
        QCOMPARE(s.targets, (QStringList{rev("HEAD"), rev("HEAD~2")}));
    }

    void upstreamOnlyLinesHaveNoSuggestion()
    {
        const FoldSuggestion s = suggestFor({{2, "two, fixed"}});
        QCOMPARE(s.status, FoldSuggestion::None);
        QVERIFY(!s.reason.isEmpty());
    }

    void upstreamLinesGoAlongWithLocalOnes()
    {
        QCOMPARE(suggestFor({{2, "two, fixed"}, {10, "ten, fixed"}}).target, rev("HEAD~1"));
    }

    void withoutUpstreamAllCommitsAreCandidates()
    {
        git({"config", "--unset", "branch.main.merge"});
        QCOMPARE(suggestFor({{2, "two, fixed"}}).target, rev("HEAD~3"));
    }

    void stopsAtMergeCommits()
    {
        git({"config", "--unset", "branch.main.merge"});
        git({"checkout", "-q", "-b", "side", "HEAD~3"});
        writeFile("side.txt", "side\n");
        commit("Side");
        git({"checkout", "-q", "main"});
        git({"merge", "-q", "--no-ff", "side", "-m", "Merge side"});
        m_changes[18] = "eighteen (L4)";
        write(lines(m_changes));
        commit("L4");

        QCOMPARE(suggestFor({{18, "eighteen, fixed"}}).target, rev("HEAD"));
        QCOMPARE(suggestFor({{5, "five, fixed"}}).status, FoldSuggestion::None);
    }

    void stagedLinesBlockUnstagedChangesOnThem()
    {
        auto staged = m_changes;
        staged[10] = "ten, staged";
        write(lines(staged));
        git({"add", "app.txt"});
        staged[10] = "ten, staged and edited";
        write(lines(staged));

        const FoldSuggestion s = analyzeAndSuggest({}, FoldBase::Index);
        QCOMPARE(s.status, FoldSuggestion::None);
        QVERIFY(s.reason.contains("not committed"_L1));
    }

    void stagedChangesAreBlamedAgainstHead()
    {
        auto staged = m_changes;
        staged[5] = "five, staged fix";
        write(lines(staged));
        git({"add", "app.txt"});
        QCOMPARE(analyzeAndSuggest({"--cached"}, FoldBase::Head).target, rev("HEAD~2"));
    }

    void newFilesAreFree()
    {
        writeFile("new.txt", "new\n");
        git({"add", "new.txt"});
        const FoldSuggestion s = analyzeAndSuggest({"--cached"}, FoldBase::Head);
        QCOMPARE(s.status, FoldSuggestion::None);
    }

private:
    static QByteArray lines(const QHash<int, QByteArray> &changes)
    {
        QByteArray text;
        for (int i = 1; i <= 20; ++i)
            text += changes.value(i, "line " + QByteArray::number(i)) + '\n';
        return text;
    }

    // Applies `modified` and `inserted` (new line after the given line
    // number) to the committed state and suggests for the whole diff.
    FoldSuggestion suggestFor(const QHash<int, QByteArray> &modified,
                              const QHash<int, QByteArray> &inserted = {})
    {
        auto changes = m_changes;
        for (auto it = modified.cbegin(); it != modified.cend(); ++it)
            changes[it.key()] = it.value();
        QByteArray text;
        const QList<QByteArray> base = lines(changes).split('\n');
        for (int i = 1; i <= 20; ++i) {
            text += base[i - 1] + '\n';
            if (inserted.contains(i))
                text += inserted[i] + '\n';
        }
        write(text);
        return analyzeAndSuggest({}, FoldBase::Index);
    }

    FoldSuggestion analyzeAndSuggest(const QStringList &diffArgs, FoldBase base)
    {
        const QList<FileDiff> files = parseDiff(git(QStringList{"diff", "--no-color", "--src-prefix=a/",
                                                                "--dst-prefix=b/"} + diffArgs));
        FoldSuggester suggester(m_git);
        suggester.analyze(files, base);
        QList<LineSelection> selection;
        for (const FileDiff &file : files)
            selection.append(selectAll(file));
        return suggester.suggest(selection);
    }

    QByteArray git(const QStringList &args)
    {
        const GitResult result = m_git.run(args);
        if (!result.ok())
            qWarning() << args << result.errorText();
        return result.out;
    }

    QString rev(const QString &r) { return QString::fromLatin1(git({"rev-parse", r}).trimmed()); }

    void write(const QByteArray &content) { writeFile("app.txt", content); }

    void writeFile(const QString &name, const QByteArray &content)
    {
        QFile file(m_dir->filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    void commit(const QString &message)
    {
        git({"add", "-A"});
        git({"commit", "-q", "-m", message});
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    GitRunner m_git;
    QHash<int, QByteArray> m_changes;
};

QTEST_GUILESS_MAIN(TestFoldSuggester)
#include "tst_foldsuggester.moc"
