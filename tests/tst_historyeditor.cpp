#include "core/HistoryEditor.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestHistoryEditor : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Default");
        qputenv("GIT_AUTHOR_EMAIL", "default@example.com");
        qputenv("GIT_COMMITTER_NAME", "Committer");
        qputenv("GIT_COMMITTER_EMAIL", "committer@example.com");
    }

    void init()
    {
        m_dir.reset(new QTemporaryDir);
        QVERIFY(m_dir->isValid());
        m_git.setWorkDir(m_dir->path());
        git({"init", "-q", "-b", "main"});

        // A: app.txt; B: notes.txt; C: changes line 9 of app.txt;
        // D: changes line 9 again, so it depends on C.
        write("app.txt", lines({}));
        commitAll("A: add app\n\nChange-Id: I0000000000000000000000000000000000000001\n",
                  "Alice", "1600000000 +0100");
        write("notes.txt", "notes\n");
        commitAll("B: add notes\n\nWhy notes.\n\nChange-Id: I0000000000000000000000000000000000000002\n",
                  "Bob", "1600001000 +0200");
        write("app.txt", lines({{9, "nine, revised"}}));
        commitAll("C: revise nine\n\nChange-Id: I0000000000000000000000000000000000000003\n",
                  "Carol", "1600002000 -0500");
        write("app.txt", lines({{9, "nine, revised twice"}}));
        commitAll("D: revise nine again\n\nChange-Id: I0000000000000000000000000000000000000004\n"
                  "Fixes: QTBUG-4\n",
                  "Dave", "1600003000 +0000");
    }

    void rewordsAndKeepsTheRest()
    {
        const QByteArray before = git({"log", "--format=%an %ad", "--date=raw"});
        const QString a = rev("HEAD~3");
        const QString b = rev("HEAD~2");

        // The Change-Id is left out of the new message; it comes back.
        HistoryEditor editor(m_git);
        const RewriteResult result = editor.edit({HistoryEdit::Reword, b, {},
                                                  u"B: add release notes\n\nWhy notes matter.\n\nTask-number: QTBUG-1"_s});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.rewritten, 3);
        QCOMPARE(rev("HEAD"), result.newHead);
        QCOMPARE(rev("HEAD~3"), a); // below the edit nothing changes
        QCOMPARE(git({"log", "-1", "--format=%B", "HEAD~2"}),
                 "B: add release notes\n\nWhy notes matter.\n\n"
                 "Task-number: QTBUG-1\nChange-Id: I0000000000000000000000000000000000000002\n\n"_ba);
        QCOMPARE(git({"log", "--format=%an %ad", "--date=raw"}), before);
        QCOMPARE(git({"log", "-1", "--format=%B", "HEAD"}),
                 "D: revise nine again\n\nChange-Id: I0000000000000000000000000000000000000004\n"
                 "Fixes: QTBUG-4\n\n"_ba);
        QCOMPARE(rev("HEAD^{tree}"), rev(result.oldHead + "^{tree}"));
        QVERIFY(!result.indexUpdated); // same tree: nothing to carry over
        QCOMPARE(git({"reflog", "-1", "--format=%gs"}), "igitt: reword " + b.left(10).toLatin1() + '\n');
    }

    void movesCommitsUpAndDown()
    {
        const QString b = rev("HEAD~2");
        const QString c = rev("HEAD~1");
        HistoryEditor editor(m_git);

        // B up past C: A C B D.
        RewriteResult result = editor.edit({HistoryEdit::Move, b, c, {}});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(subjects(), subjectsOf("DBCA"));
        QCOMPARE(rev("HEAD^{tree}"), rev(result.oldHead + "^{tree}"));
        QCOMPARE(git({"show", "HEAD~2:app.txt"}), lines({{9, "nine, revised"}}));
        QVERIFY(!git({"ls-tree", "--name-only", "HEAD~2"}).contains("notes.txt"));

        // And back down: A B C D, the commits as they were apart from the
        // committer.
        result = editor.edit({HistoryEdit::Move, rev("HEAD~1"), rev("HEAD~2"), {}});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(subjects(), subjectsOf("DCBA"));
        QCOMPARE(rev("HEAD~2^{tree}"), rev(b + "^{tree}"));
        QCOMPARE(rev("HEAD~1^{tree}"), rev(c + "^{tree}"));
    }

    void movesTheRootCommit()
    {
        const QString a = rev("HEAD~3");
        const QString b = rev("HEAD~2");
        HistoryEditor editor(m_git);
        const RewriteResult result = editor.edit({HistoryEdit::Move, a, b, {}});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(subjects(), subjectsOf("DCAB"));
        QCOMPARE(git({"ls-tree", "--name-only", "HEAD~3"}), "notes.txt\n"_ba);
        QCOMPARE(git({"rev-list", "--max-parents=0", "HEAD"}).trimmed().size(), 40);
    }

    void squashesKeepingBothMessages()
    {
        HistoryEditor editor(m_git);
        const RewriteResult result = editor.edit({HistoryEdit::Squash, rev("HEAD"), {}, {}});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.rewritten, 1);
        QCOMPARE(subjects(), subjectsOf("CBA"));
        QCOMPARE(git({"log", "-1", "--format=%an %B"}),
                 "Carol C: revise nine\n\nD: revise nine again\n\n"
                 "Change-Id: I0000000000000000000000000000000000000003\nFixes: QTBUG-4\n\n"_ba);
        QCOMPARE(git({"show", "HEAD:app.txt"}), lines({{9, "nine, revised twice"}}));
        // Both old commits now are the combined one.
        QCOMPARE(result.rewrites.size(), 2);
        QCOMPARE(result.rewrites[0].second, result.newHead);
        QCOMPARE(result.rewrites[1].second, result.newHead);
    }

    void fixesUpKeepingThePreviousMessage()
    {
        const QString d = rev("HEAD");
        HistoryEditor editor(m_git);
        const RewriteResult result = editor.edit({HistoryEdit::Fixup, rev("HEAD~2"), {}, {}});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(subjects(), subjectsOf("DCA"));
        QCOMPARE(git({"log", "-1", "--format=%B", "HEAD~2"}),
                 "A: add app\n\nChange-Id: I0000000000000000000000000000000000000001\n\n"_ba);
        QCOMPARE(git({"ls-tree", "--name-only", "HEAD~2"}), "app.txt\nnotes.txt\n"_ba);
        QCOMPARE(rev("HEAD^{tree}"), rev(d + "^{tree}"));
    }

    void dropsAndKeepsTheContentAsAChange()
    {
        write("other.txt", "staged\n");
        git({"add", "other.txt"});
        const QString b = rev("HEAD~2");
        HistoryEditor editor(m_git);
        const RewriteResult result = editor.edit({HistoryEdit::Drop, b, {}, {}});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(subjects(), subjectsOf("DCA"));
        QVERIFY(!git({"ls-tree", "--name-only", "HEAD"}).contains("notes.txt"));
        QCOMPARE(result.rewrites.first(), std::make_pair(b, QString()));

        // The working tree is untouched; the dropped file is untracked now,
        // and what was staged still is.
        QCOMPARE(read("notes.txt"), "notes\n"_ba);
        QVERIFY(result.indexUpdated);
        QCOMPARE(git({"status", "--porcelain"}), "A  other.txt\n?? notes.txt\n"_ba);
    }

    void dropsTheRootCommit()
    {
        HistoryEditor editor(m_git);
        // C changes the file A adds; without A that is a conflict.
        QVERIFY(!editor.edit({HistoryEdit::Drop, rev("HEAD~3"), {}, {}}).ok);

        // B, moved to the bottom, depends on nothing.
        QVERIFY(editor.edit({HistoryEdit::Move, rev("HEAD~2"), rev("HEAD~3"), {}}).ok);
        QCOMPARE(subjects(), subjectsOf("DCAB"));
        const RewriteResult result = editor.edit({HistoryEdit::Drop, rev("HEAD~3"), {}, {}});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(subjects(), subjectsOf("DCA"));
        QCOMPARE(git({"ls-tree", "--name-only", "HEAD~2"}), "app.txt\n"_ba);
        QCOMPARE(git({"rev-list", "--max-parents=0", "HEAD"}).trimmed(), rev("HEAD~2").toLatin1());
    }

    void conflictLeavesEverythingUntouched()
    {
        // D changes the line C changed; it cannot come before C.
        write("app.txt", lines({{9, "nine, revised twice"}, {2, "unstaged"}}));
        const QString head = rev("HEAD");
        const QByteArray status = git({"status", "--porcelain"});
        HistoryEditor editor(m_git);
        const RewriteResult result = editor.edit({HistoryEdit::Move, rev("HEAD"), rev("HEAD~1"), {}});
        QVERIFY(!result.ok);
        QCOMPARE(result.conflicts, QStringList{u"app.txt"_s});
        QCOMPARE(rev("HEAD"), head);
        QCOMPARE(git({"status", "--porcelain"}), status);

        // Dropping C has D's change apply to A's version: also a conflict.
        QVERIFY(!editor.edit({HistoryEdit::Drop, rev("HEAD~1"), {}, {}}).ok);
        QCOMPARE(rev("HEAD"), head);
    }

    void refusesWhatItCannotDo()
    {
        HistoryEditor editor(m_git);
        QVERIFY(!editor.edit({HistoryEdit::Squash, rev("HEAD~3"), {}, {}}).ok); // root: nothing before it
        QVERIFY(!editor.edit({HistoryEdit::Reword, rev("HEAD"), {}, u"\n\n"_s}).ok);

        // A todo list must name the latest commits exactly.
        QString error;
        QList<RewriteStep> steps = editor.todo(rev("HEAD~1"), u"HEAD"_s, error);
        QCOMPARE(steps.size(), 2);
        steps.removeLast();
        QVERIFY(!editor.rewrite(steps).ok);

        git({"checkout", "-q", "-b", "side", "HEAD~2"});
        write("side.txt", "side\n");
        commitAll("S: side\n", "Sam", "1600004000 +0000");
        git({"checkout", "-q", "main"});
        QVERIFY(!editor.blocker(rev("side")).isEmpty());
        QVERIFY(!editor.edit({HistoryEdit::Drop, rev("side"), {}, {}}).ok);

        git({"merge", "-q", "--no-ff", "side", "-m", "Merge side"});
        const QString head = rev("HEAD");
        QVERIFY(!editor.edit({HistoryEdit::Reword, rev("HEAD~1"), {}, u"C: below a merge"_s}).ok);
        QVERIFY(!editor.edit({HistoryEdit::Move, rev("HEAD"), rev("HEAD~1"), {}}).ok);
        QCOMPARE(rev("HEAD"), head);
        // The merge itself can be reworded; it keeps both parents.
        const RewriteResult reworded = editor.edit({HistoryEdit::Reword, head, {}, u"Merge the side"_s});
        QVERIFY2(reworded.ok, qPrintable(reworded.error));
        QCOMPARE(rev("HEAD^2"), rev("side"));
        QCOMPARE(git({"log", "-1", "--format=%s"}), "Merge the side\n"_ba);
    }

    void refusesDuringARebase()
    {
        git({"checkout", "-q", "-b", "side", "HEAD~1"});
        write("app.txt", lines({{9, "nine, on the side"}}));
        commitAll("S: side\n", "Sam", "1600004000 +0000");
        git({"rebase", "-q", "main"}); // stops with a conflict
        HistoryEditor editor(m_git);
        QVERIFY(!editor.blocker(rev("main~1"), rev("main")).isEmpty());
        QVERIFY(!editor.edit({HistoryEdit::Drop, rev("main~1"), {}, {}}, rev("main")).ok);
    }

    void squashMessageJoinsTrailers()
    {
        QCOMPARE(HistoryEditor::squashMessage(u"Fix it\n\nBody.\n\nChange-Id: I1\nFixes: QTBUG-1"_s,
                                              u"Fix more\n\nChange-Id: I2\nFixes: QTBUG-1\nPick-to: 6.8"_s),
                 u"Fix it\n\nBody.\n\nFix more\n\nChange-Id: I1\nFixes: QTBUG-1\nPick-to: 6.8\n"_s);
        QCOMPARE(HistoryEditor::keepChangeId(u"New\n\nFixes: X"_s, u"Old\n\nChange-Id: I1"_s),
                 u"New\n\nFixes: X\nChange-Id: I1\n"_s);
        QCOMPARE(HistoryEditor::keepChangeId(u"New\n\nChange-Id: I2"_s, u"Old\n\nChange-Id: I1"_s),
                 u"New\n\nChange-Id: I2\n"_s);
    }

private:
    static QByteArray lines(const QHash<int, QByteArray> &changes)
    {
        QByteArray text;
        for (int i = 1; i <= 10; ++i)
            text += changes.value(i, QByteArray::number(i)) + '\n';
        return text;
    }

    // The subjects' leading letters, newest first.
    QString subjects()
    {
        QString letters;
        for (const QByteArray &line : git({"log", "--format=%s"}).split('\n')) {
            if (!line.isEmpty())
                letters += QLatin1Char(line.at(0));
        }
        return letters;
    }
    static QString subjectsOf(const char *letters) { return QString::fromLatin1(letters); }

    QByteArray git(const QStringList &args, const QProcessEnvironment &env = {})
    {
        const GitResult result = m_git.run(args, {}, env);
        if (!result.ok() && args.first() != "rebase"_L1)
            qWarning() << args << result.errorText();
        return result.out;
    }

    QString rev(const QString &r) { return QString::fromLatin1(git({"rev-parse", r}).trimmed()); }

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

    void commitAll(const QString &message, const QString &author, const QString &date)
    {
        QProcessEnvironment env;
        env.insert(u"GIT_AUTHOR_NAME"_s, author);
        env.insert(u"GIT_AUTHOR_EMAIL"_s, author.toLower() + u"@example.com"_s);
        env.insert(u"GIT_AUTHOR_DATE"_s, date);
        git({"add", "-A"});
        git({"commit", "-q", "--cleanup=verbatim", "-m", message}, env);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    GitRunner m_git;
};

QTEST_GUILESS_MAIN(TestHistoryEditor)
#include "tst_historyeditor.moc"
