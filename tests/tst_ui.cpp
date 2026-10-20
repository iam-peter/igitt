// Loads the real UI against a small repository and fails on any QML warning.
// Set IGITT_SCREENSHOT_DIR to also save screenshots of the window.

#include "Repository.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Igitt_CorePlugin)
Q_IMPORT_QML_PLUGIN(IgittPlugin)

using namespace Qt::StringLiterals;

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qputenv("GIT_CONFIG_GLOBAL", "/dev/null");
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        qputenv("GIT_AUTHOR_NAME", "Ada Lovelace");
        qputenv("GIT_AUTHOR_EMAIL", "ada@example.com");
        qputenv("GIT_COMMITTER_NAME", "Ada Lovelace");
        qputenv("GIT_COMMITTER_EMAIL", "ada@example.com");
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQuickStyle::setStyle(u"Fusion"_s);
        createDemoRepository();
    }

    void loadsWithoutWarnings()
    {
        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &w) { warnings += w; });

        engine.setInitialProperties({
            {u"initialPath"_s, m_dir.path()},
            {u"monoFont"_s, QFontDatabase::systemFont(QFontDatabase::FixedFont)},
        });
        engine.loadFromModule("Igitt", "Main");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *repo = window->findChild<Repository *>();
        QVERIFY(repo);
        QCOMPARE(repo->commits()->rowCount(), 7);
        QCOMPARE(repo->unstaged()->count(), 2);
        QCOMPARE(repo->staged()->count(), 1);

        repo->showFile(u"app.txt"_s, false);
        DiffModel *diff = repo->diff();
        for (int row = 0; row < diff->rowCount(); ++row) {
            if (diff->data(diff->index(row), DiffModel::TextRole).toString().startsWith("line 4"_L1))
                diff->toggleLine(row);
        }
        QTRY_COMPARE(diff->selectedCount(), 2);
        screenshot(window, u"working-tree"_s);

        repo->showCommit(repo->commits()->shaAt(3));
        QTest::qWait(50);
        screenshot(window, u"commit"_s);

        for (const QQmlError &w : std::as_const(warnings))
            qWarning().noquote() << w.toString();
        QVERIFY(warnings.isEmpty());
    }

private:
    void git(const QStringList &args)
    {
        const GitResult result = GitRunner(m_dir.path()).run(args);
        QVERIFY2(result.ok(), qPrintable(result.errorText()));
    }

    void write(const QString &name, const QByteArray &content)
    {
        QFile file(m_dir.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    QByteArray appText(const QHash<int, QByteArray> &changes)
    {
        QByteArray text;
        for (int i = 1; i <= 30; ++i)
            text += "line " + QByteArray::number(i) + changes.value(i) + '\n';
        return text;
    }

    void createDemoRepository()
    {
        QHash<int, QByteArray> changes;
        git({"init", "-q", "-b", "main"});
        write(u"app.txt"_s, appText(changes));
        write(u"README.md"_s, "# Demo\n");
        git({"add", "."});
        git({"commit", "-q", "-m", "Initial import"});
        git({"tag", "v0.1"});

        changes[3] = " parsed";
        write(u"app.txt"_s, appText(changes));
        git({"commit", "-q", "-a", "-m", "Parse line three"});

        git({"checkout", "-q", "-b", "feature"});
        write(u"feature.txt"_s, "feature\n");
        git({"add", "feature.txt"});
        git({"commit", "-q", "-m", "Add feature skeleton"});
        auto featureChanges = changes;
        featureChanges[20] = " feature";
        write(u"app.txt"_s, appText(featureChanges));
        git({"commit", "-q", "-a", "-m", "Wire feature into app"});

        git({"checkout", "-q", "main"});
        changes[10] = " fixed";
        write(u"app.txt"_s, appText(changes));
        git({"commit", "-q", "-a", "-m", "Fix line ten"});
        git({"merge", "-q", "--no-ff", "feature", "-m", "Merge branch 'feature'"});
        changes[20] = " feature";
        changes[25] = " tuned";
        write(u"app.txt"_s, appText(changes));
        git({"commit", "-q", "-a", "-m", "Tune line 25"});

        changes[4] = " changed";
        changes[21] = " changed";
        write(u"app.txt"_s, appText(changes));
        write(u"notes.txt"_s, "untracked\n");
        write(u"README.md"_s, "# Demo\n\nstaged\n");
        git({"add", "README.md"});
    }

    void screenshot(QQuickWindow *window, const QString &name)
    {
        const QString dir = qEnvironmentVariable("IGITT_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        QVERIFY(window->grabWindow().save(QDir(dir).filePath(name + u".png"_s)));
    }

    QTemporaryDir m_dir;
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
