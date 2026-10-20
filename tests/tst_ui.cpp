// Loads the real UI against a small repository and fails on any QML warning.
// Set IGITT_SCREENSHOT_DIR to also save screenshots of the window.

#include "Repository.h"
#include "models/ConflictResolver.h"
#include "models/Timelapse.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Igitt_CorePlugin)
Q_IMPORT_QML_PLUGIN(IgittPlugin)

using namespace Qt::StringLiterals;

namespace {

// Waits until every queued git job has run and its result is applied.
[[nodiscard]] bool settle(Repository &repo)
{
    return QTest::qWaitFor([&repo] { return !repo.busy(); }, 10000);
}

} // namespace

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
        // Settings go to a throwaway location, not the user's.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(u"igitt-test"_s);
        QCoreApplication::setOrganizationDomain(u"igitt.test"_s);
        QSettings().clear();
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
        QVERIFY(settle(*repo));
        QCOMPARE(repo->commits()->rowCount(), 9);
        QCOMPARE(repo->unstaged()->count(), 2);
        QCOMPARE(repo->staged()->count(), 1);

        repo->showFile(u"app.txt"_s, false);
        QVERIFY(settle(*repo));
        DiffModel *diff = repo->diff();
        for (int row = 0; row < diff->rowCount(); ++row) {
            if (diff->data(diff->index(row), DiffModel::TextRole).toString().startsWith("line 4"_L1))
                diff->toggleLine(row);
        }
        QTRY_COMPARE(diff->selectedCount(), 2);
        screenshot(window, u"working-tree"_s);
        rightClickDiffRow(window, 5);
        screenshot(window, u"line-menu"_s);
        QTest::keyClick(window, Qt::Key_Escape);

        dragSelectionOntoCommit(window, 2);
        QVERIFY(settle(*repo));
        QVERIFY(repo->canUndoFold());
        QCOMPARE(repo->commits()->rowCount(), 9);
        QVERIFY(gitOut({"show", "HEAD~2", "--format="}).contains("+line 4 changed"));
        QVERIFY(!gitOut({"diff"}).contains("+line 4 changed"));
        QVERIFY(gitOut({"diff"}).contains("+line 21 changed"));
        auto *messageBar = window->findChild<QQuickItem *>(u"messageBar"_s);
        QVERIFY(messageBar && messageBar->isVisible());
        screenshot(window, u"folded"_s);

        repo->undoFold();
        QVERIFY(settle(*repo));
        QVERIFY(gitOut({"diff"}).contains("+line 4 changed"));

        // Line 16 borders line 15, last changed by "Document line 15".
        diff->clearSelection();
        for (int row = 0; row < diff->rowCount(); ++row) {
            if (diff->data(diff->index(row), DiffModel::TextRole).toString().startsWith("line 16"_L1))
                diff->toggleLine(row);
        }
        QCOMPARE(repo->suggestedFoldTarget(), repo->commits()->shaAt(1));
        auto *suggestButton = window->findChild<QQuickItem *>(u"foldSuggestedButton"_s);
        QVERIFY(suggestButton);
        QTRY_VERIFY(suggestButton->isVisible());
        screenshot(window, u"suggested"_s);
        QTest::mouseClick(window, Qt::LeftButton, {},
                          suggestButton->mapToScene({suggestButton->width() / 2, suggestButton->height() / 2}).toPoint());
        QVERIFY(settle(*repo));
        QVERIFY(gitOut({"show", "HEAD~1", "--format="}).contains("+line 16 changed"));
        QVERIFY(gitOut({"diff"}).contains("+line 4 changed"));

        repo->showCommit(repo->commits()->shaAt(3));
        QVERIFY(settle(*repo));
        QTest::qWait(50);
        screenshot(window, u"commit"_s);

        // Time-lapse of app.txt, opened as from a file's context menu.
        QVERIFY(QMetaObject::invokeMethod(window, "openTimelapse", Q_ARG(QString, u"app.txt"_s)));
        QQuickWindow *lapseWindow = nullptr;
        QTRY_VERIFY((lapseWindow = timelapseWindow()));
        QVERIFY(QTest::qWaitForWindowExposed(lapseWindow));
        auto *lapse = lapseWindow->findChild<Timelapse *>();
        QVERIFY(lapse);
        QTRY_COMPARE(lapse->revisionCount(), 7); // every commit that touched app.txt
        QTRY_VERIFY(!lapse->busy() && lapse->rowCount() >= 30);
        screenshot(lapseWindow, u"timelapse"_s);
        lapse->setRevision(lapse->revisionCount() - 2);
        QTRY_VERIFY(!lapse->busy());
        screenshot(lapseWindow, u"timelapse-previous"_s);
        lapseWindow->close();
        QTRY_VERIFY(!timelapseWindow());

        // Searching dims what doesn't match; the minimap is above the graph.
        auto *search = window->findChild<QQuickItem *>(u"historySearch"_s);
        QVERIFY(search);
        search->setProperty("text", u"message:line author:ada"_s);
        QTRY_COMPARE(repo->commits()->matchCount(), 5);
        QVERIFY(window->findChild<QQuickItem *>(u"minimap"_s)->isVisible());
        screenshot(window, u"search"_s);
        search->setProperty("text", QString());
        QVERIFY(!repo->commits()->searching());

        // Plan mode: stage and commit, review, execute.
        const QByteArray realHead = gitOut({"rev-parse", "HEAD"});
        click(window, u"planButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(repo->planning());
        repo->showFile(u"app.txt"_s, false);
        QVERIFY(settle(*repo));
        for (int row = 0; row < diff->rowCount(); ++row) {
            if (diff->data(diff->index(row), DiffModel::TextRole).toString().startsWith("line 21"_L1))
                diff->toggleLine(row);
        }
        repo->applySelection();
        QVERIFY(settle(*repo));
        repo->commit(u"Change line 21"_s, false);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->plan()->count(), 2);
        QCOMPARE(gitOut({"rev-parse", "HEAD"}), realHead);
        screenshot(window, u"plan"_s);

        click(window, u"executePlanButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(!repo->planning());
        QCOMPARE(gitOut({"log", "-1", "--format=%s"}), "Change line 21\n"_ba);
        screenshot(window, u"executed"_s);

        // Files: stage from the context menu, move between the lists by drag and drop.
        // The planned commit took everything that was staged.
        QVERIFY(gitOut({"diff", "--cached", "--name-only"}).isEmpty());
        QQuickItem *notes = fileRow(window, u"unstagedList"_s, u"notes.txt"_s);
        QVERIFY(notes);
        QTest::mouseClick(window, Qt::RightButton, {},
                          notes->mapToScene({notes->width() / 2, notes->height() / 2}).toPoint());
        click(window, u"stageFileMenuItem"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(gitOut({"diff", "--cached", "--name-only"}), "notes.txt\n"_ba);

        dragFile(window, u"unstagedList"_s, u"app.txt"_s, u"stagedList"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(gitOut({"diff", "--cached", "--name-only"}), "app.txt\nnotes.txt\n"_ba);

        dragFile(window, u"stagedList"_s, u"notes.txt"_s, u"unstagedList"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(gitOut({"diff", "--cached", "--name-only"}), "app.txt\n"_ba);

        // The commit form: title, a body that needs wrapping, a trailer.
        auto *box = window->findChild<QQuickItem *>(u"commitBox"_s);
        QVERIFY(box);
        window->findChild<QQuickItem *>(u"commitTitle"_s)->setProperty("text", u"Change line four"_s);
        window->findChild<QQuickItem *>(u"commitBody"_s)->setProperty(
            "text", u"This explains at length why line four had to change, far beyond seventy-two characters."_s);
        QVERIFY(QMetaObject::invokeMethod(box, "addTrailer", Q_ARG(QString, u"Task-number"_s)));
        QTRY_VERIFY(findItem(window->contentItem(), u"trailerValue"_s));
        findItem(window->contentItem(), u"trailerValue"_s)->setProperty("text", u"QTBUG-123"_s);
        screenshot(window, u"commit-form"_s);
        click(window, u"commitButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(gitOut({"log", "-1", "--format=%B"}).trimmed(),
                 "Change line four\n\n"
                 "This explains at length why line four had to change, far beyond\n"
                 "seventy-two characters.\n\n"
                 "Task-number: QTBUG-123"_ba);
        QCOMPARE(window->findChild<QQuickItem *>(u"commitTitle"_s)->property("text").toString(), QString());

        // A merge conflict, resolved in the merge tool.
        git({"add", "-A"});
        git({"commit", "-q", "-m", "Checkpoint"});
        write(u"conflict.txt"_s, "one\ntwo\nthree\n");
        git({"add", "conflict.txt"});
        git({"commit", "-q", "-m", "Add conflict file"});
        git({"checkout", "-q", "-b", "other"});
        write(u"conflict.txt"_s, "one\nTWO other\nthree\n");
        git({"commit", "-q", "-a", "-m", "Other change"});
        git({"checkout", "-q", "main"});
        write(u"conflict.txt"_s, "one\nTWO main\nthree\n");
        git({"commit", "-q", "-a", "-m", "Main change"});
        gitOut({"merge", "-q", "other"}); // stops with a conflict
        repo->refresh();
        QVERIFY(settle(*repo));
        repo->showFile(u"conflict.txt"_s, false);
        QVERIFY(settle(*repo));
        QVERIFY(repo->diffConflicted());

        ConflictResolver *resolver = nullptr;
        QTRY_VERIFY((resolver = window->findChild<ConflictResolver *>()) && !resolver->busy());
        QCOMPARE(resolver->conflictCount(), 1);
        QTRY_VERIFY(findItem(window->contentItem(), u"takeTheirs"_s));
        clickItem(window, findItem(window->contentItem(), u"takeTheirs"_s));
        clickItem(window, findItem(window->contentItem(), u"takeOurs"_s));
        QCOMPARE(resolver->output(), u"one\nTWO other\nTWO main\nthree\n"_s);
        screenshot(window, u"conflict"_s);
        click(window, u"saveResolution"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(gitOut({"show", ":conflict.txt"}), "one\nTWO other\nTWO main\nthree\n"_ba);
        QVERIFY(gitOut({"ls-files", "-u"}).isEmpty());

        for (const QQmlError &w : std::as_const(warnings))
            qWarning().noquote() << w.toString();
        QVERIFY(warnings.isEmpty());
    }

private:
    // Drags the fold handle of the diff view onto a row of the history.
    void dragSelectionOntoCommit(QQuickWindow *window, int row)
    {
        auto *handle = window->findChild<QQuickItem *>(u"foldHandle"_s);
        auto *list = window->findChild<QQuickItem *>(u"historyList"_s);
        QVERIFY(handle && list);
        QTRY_VERIFY(handle->isVisible());
        QQuickItem *rowItem = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, rowItem),
                                          Q_ARG(int, row)));
        QVERIFY(rowItem);

        const QPoint from = handle->mapToScene({handle->width() / 2, handle->height() / 2}).toPoint();
        const QPoint to = rowItem->mapToScene({rowItem->width() / 3, rowItem->height() / 2}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        constexpr int steps = 12;
        for (int i = 1; i <= steps; ++i)
            QTest::mouseMove(window, from + (to - from) * i / steps);
        QTest::qWait(50);
        screenshot(window, u"dragging"_s);
        QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    }

    void click(QQuickWindow *window, const QString &objectName)
    {
        auto *item = window->findChild<QQuickItem *>(objectName);
        QVERIFY2(item, qPrintable(objectName));
        QTRY_VERIFY(item->isVisible() && item->isEnabled());
        QTest::mouseClick(window, Qt::LeftButton, {},
                          item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    }

    static QQuickWindow *timelapseWindow()
    {
        for (QWindow *w : QGuiApplication::topLevelWindows()) {
            if (w->isVisible() && w->title().startsWith("Time-lapse"_L1))
                return qobject_cast<QQuickWindow *>(w);
        }
        return nullptr;
    }

    // Items a Repeater creates have no QObject parent; walk the item tree.
    // Only visible items count.
    static QQuickItem *findItem(QQuickItem *root, const QString &objectName)
    {
        if (!root->isVisible())
            return nullptr;
        if (root->objectName() == objectName)
            return root;
        for (QQuickItem *child : root->childItems()) {
            if (QQuickItem *found = findItem(child, objectName))
                return found;
        }
        return nullptr;
    }

    QQuickItem *fileRow(QQuickWindow *window, const QString &list, const QString &path)
    {
        auto *view = window->findChild<QQuickItem *>(list);
        if (!view)
            return nullptr;
        for (int row = 0; row < view->property("count").toInt(); ++row) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, row));
            if (item && item->property("path").toString() == path)
                return item;
        }
        return nullptr;
    }

    // Drags a file row onto the middle of the other list.
    void dragFile(QQuickWindow *window, const QString &fromList, const QString &path, const QString &toList)
    {
        QQuickItem *row = fileRow(window, fromList, path);
        auto *target = window->findChild<QQuickItem *>(toList);
        QVERIFY2(row && target, qPrintable(path));
        const QPoint from = row->mapToScene({row->width() / 2, row->height() / 2}).toPoint();
        const QPoint to = target->mapToScene({target->width() / 2, target->height() / 2}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        constexpr int steps = 10;
        for (int i = 1; i <= steps; ++i)
            QTest::mouseMove(window, from + (to - from) * i / steps);
        QTest::qWait(30);
        screenshot(window, u"file-drag"_s);
        QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    }

    static void clickItem(QQuickWindow *window, QQuickItem *item)
    {
        QVERIFY(item && item->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, {},
                          item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    }

    void rightClickDiffRow(QQuickWindow *window, int row)
    {
        auto *list = window->findChild<QQuickItem *>(u"diffList"_s);
        QVERIFY(list);
        QQuickItem *rowItem = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, rowItem),
                                          Q_ARG(int, row)));
        QVERIFY(rowItem);
        QTest::mouseClick(window, Qt::RightButton, {},
                          rowItem->mapToScene({rowItem->width() / 4, rowItem->height() / 2}).toPoint());
        QTest::qWait(50);
    }

    QByteArray gitOut(const QStringList &args)
    {
        return GitRunner(m_dir.path()).run(args).out;
    }

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

    // Spreads the demo history over months, as a real one would be.
    static void setDate(const char *iso)
    {
        qputenv("GIT_AUTHOR_DATE", iso);
        qputenv("GIT_COMMITTER_DATE", iso);
    }

    void createDemoRepository()
    {
        QHash<int, QByteArray> changes;
        git({"init", "-q", "-b", "main"});
        write(u"app.txt"_s, appText(changes));
        write(u"README.md"_s, "# Demo\n");
        git({"add", "."});
        setDate("2026-03-02T10:00:00");
        git({"commit", "-q", "-m", "Initial import"});
        git({"tag", "v0.1"});

        changes[3] = " parsed";
        write(u"app.txt"_s, appText(changes));
        setDate("2026-03-20T10:00:00");
        git({"commit", "-q", "-a", "-m", "Parse line three"});

        git({"checkout", "-q", "-b", "feature"});
        write(u"feature.txt"_s, "feature\n");
        git({"add", "feature.txt"});
        setDate("2026-04-11T10:00:00");
        git({"commit", "-q", "-m", "Add feature skeleton"});
        auto featureChanges = changes;
        featureChanges[20] = " feature";
        write(u"app.txt"_s, appText(featureChanges));
        setDate("2026-04-12T15:00:00");
        git({"commit", "-q", "-a", "-m", "Wire feature into app"});

        git({"checkout", "-q", "main"});
        changes[10] = " fixed";
        write(u"app.txt"_s, appText(changes));
        setDate("2026-05-05T10:00:00");
        git({"commit", "-q", "-a", "-m", "Fix line ten"});
        setDate("2026-05-06T10:00:00");
        git({"merge", "-q", "--no-ff", "feature", "-m", "Merge branch 'feature'"});
        changes[20] = " feature";
        changes[25] = " tuned";
        write(u"app.txt"_s, appText(changes));
        setDate("2026-06-19T10:00:00");
        git({"commit", "-q", "-a", "-m", "Tune line 25"});

        changes[15] = " documented";
        write(u"app.txt"_s, appText(changes));
        setDate("2026-08-01T10:00:00");
        git({"commit", "-q", "-a", "-m", "Document line 15"});
        changes[28] = " polished";
        write(u"app.txt"_s, appText(changes));
        setDate("2026-09-15T10:00:00");
        git({"commit", "-q", "-a", "-m", "Polish line 28"});

        changes[4] = " changed";
        changes[16] = " changed";
        changes[21] = " changed";
        write(u"app.txt"_s, appText(changes));
        write(u"notes.txt"_s, "untracked\n");
        qunsetenv("GIT_AUTHOR_DATE");
        qunsetenv("GIT_COMMITTER_DATE");
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
