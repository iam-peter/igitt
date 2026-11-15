// Loads the real UI against a small repository and fails on any QML warning.
// Set IGITT_SCREENSHOT_DIR to also save screenshots of the window, and
// IGITT_TEST_THEME to show them in another theme than the light one
// (dracula, catppuccin-mocha, ...).

#include "Repository.h"
#include "core/AskPass.h"
#include "core/ChangeClusters.h"
#include "core/GitRunner.h"
#include "core/LaneColors.h"
#include "themes.h"
#include "models/ConflictResolver.h"
#include "ui/Fonts.h"
#include "models/Timelapse.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QWheelEvent>
#include <QQuickItem>
#include <QtQuickTest/quicktest.h>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <functional>

Q_IMPORT_QML_PLUGIN(Igitt_CorePlugin)
Q_IMPORT_QML_PLUGIN(IgittPlugin)
Q_IMPORT_QML_PLUGIN(IgittStylePlugin)

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
        if (const QString theme = qEnvironmentVariable("IGITT_TEST_THEME"); !theme.isEmpty())
            QSettings().setValue(u"appearance/theme"_s, theme);
        if (qEnvironmentVariableIsEmpty("IGITT_TEST_DEFAULT_RHI"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQuickStyle::setStyle(u"IgittStyle"_s);
        QQuickStyle::setFallbackStyle(u"Basic"_s);
        Fonts::registerBundled();
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
        });
        engine.loadFromModule("Igitt", "Main");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *repo = window->findChild<Repository *>();
        QVERIFY(repo);
        QVERIFY(settle(*repo));
        // The bundled fonts are in use.
        QCOMPARE(window->property("font").value<QFont>().family(), u"Inter"_s);
        QCOMPARE(window->property("monoFont").value<QFont>().family(), u"JetBrains Mono"_s);
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

        // Dragged onto a commit of the strip above the changes
        dragSelectionOntoCommit(window, u"recentCommits"_s, 2);
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

        // History edits, in Compose: reword, move and drop from a commit's
        // menu, drop with confirmation; each can be undone.
        {
            const QByteArray before = gitOut({"log", "--format=%s"});
            // The commit's column, first on the board, and its menu
            const auto openCommitMenu = [&](int row) {
                const QString sha = repo->commits()->shaAt(row);
                QVERIFY(QMetaObject::invokeMethod(window, "editInCompose", Q_ARG(QString, sha)));
                QQuickItem *header = nullptr;
                QTRY_VERIFY((header = [&]() -> QQuickItem * {
                    for (QQuickItem *column : findItems(window->contentItem(), u"commitColumn"_s)) {
                        if (column->isVisible() && column->property("modelData").toMap().value(u"sha"_s) == sha)
                            return findItem(column, u"commitHeader"_s);
                    }
                    return nullptr;
                }()));
                QVERIFY(QQuickTest::qWaitForPolish(window));
                QTRY_VERIFY(header->mapToScene({0, 0}).x() >= 0
                            && header->mapToScene({header->width(), 0}).x() <= window->width());
                QTest::mouseClick(window, Qt::RightButton, {}, header->mapToScene({header->width() / 3, header->height() / 2}).toPoint());
                QTRY_VERIFY(window->property("popupOpen").toBool());
            };
            openCommitMenu(1);
            screenshot(window, u"commit-menu"_s);
            click(window, u"rewordMenuItem"_s);
            auto *rewordTitle = window->findChild<QQuickItem *>(u"rewordTitle"_s);
            QVERIFY(rewordTitle);
            QTRY_COMPARE(rewordTitle->property("text").toString(), u"Document line 15"_s);
            rewordTitle->setProperty("text", u"Document line fifteen"_s);
            screenshot(window, u"reword"_s);
            click(window, u"rewordOkButton"_s);
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"log", "-1", "--format=%s", "HEAD~1"}), "Document line fifteen\n"_ba);
            QVERIFY(repo->canUndoFold());
            repo->undoFold();
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"log", "--format=%s"}), before);

            openCommitMenu(0);
            click(window, u"moveDownMenuItem"_s);
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"log", "-2", "--format=%s"}), "Document line 15\nPolish line 28\n"_ba);
            repo->undoFold();
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"log", "--format=%s"}), before);

            openCommitMenu(0);
            click(window, u"dropMenuItem"_s);
            click(window, u"dropOkButton"_s);
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"log", "-1", "--format=%s"}), "Document line 15\n"_ba);
            // The working tree still has the change; it is uncommitted now.
            QVERIFY(gitOut({"diff"}).contains("+line 28 polished"));
            repo->undoFold();
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"log", "--format=%s"}), before);
            QVERIFY(!gitOut({"diff"}).contains("line 28"));
            QVERIFY(gitOut({"diff"}).contains("+line 4 changed"));
            QCOMPARE(gitOut({"diff", "--cached", "--name-only"}), "README.md\n"_ba);
        }
        QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));

        repo->showCommit(repo->commits()->shaAt(3));
        QVERIFY(settle(*repo));
        QTest::qWait(50);
        screenshot(window, u"commit"_s);

        // A commit's files collapse and expand, all at once or one by one.
        QCOMPARE(repo->diff()->fileCount(), 2);
        click(window, u"collapseAllButton"_s);
        QTRY_COMPARE(repo->diff()->rowCount(), 2);
        screenshot(window, u"collapsed"_s);
        click(window, u"expandAllButton"_s);
        QTRY_VERIFY(repo->diff()->rowCount() > 2);
        repo->diff()->toggleFile(0);
        QCOMPARE(repo->diff()->collapsedCount(), 1);
        repo->diff()->setAllCollapsed(false);

        // Code is highlighted, and the toolbar turns that off and on.
        write(u"main.cpp"_s, "#include <cstdio>\n\n// Says hello.\nint main()\n{\n"
                             "    return std::puts(\"hello\") < 0 ? 1 : 0;\n}\n");
        repo->refresh();
        QVERIFY(settle(*repo));
        repo->showFile(u"main.cpp"_s, false);
        QVERIFY(settle(*repo));
        QQuickItem *code = diffLine(window, u"+int main()"_s);
        QVERIFY(code);
        QTRY_VERIFY(code->property("coloredSpans").toInt() > 0);
        screenshot(window, u"highlighted"_s);
        click(window, u"highlightButton"_s);
        QTRY_COMPARE(code->property("coloredSpans").toInt(), 0);
        click(window, u"highlightButton"_s);
        QTRY_VERIFY(code->property("coloredSpans").toInt() > 0);
        QVERIFY(QFile::remove(m_dir.filePath(u"main.cpp"_s)));
        repo->refresh();
        QVERIFY(settle(*repo));

        // So are the words that changed.
        repo->showFile(u"app.txt"_s, false);
        QVERIFY(settle(*repo));
        // A single file collapses and expands too.
        QCOMPARE(repo->diff()->fileCount(), 1);
        QVERIFY(window->findChild<QQuickItem *>(u"collapseAllButton"_s)->isVisible());
        QVERIFY(window->findChild<QQuickItem *>(u"expandAllButton"_s)->isVisible());
        QQuickItem *changed = diffLine(window, u"+line 4 changed"_s);
        QVERIFY(changed);
        QTRY_COMPARE(changed->property("markedChanges").toInt(), 1);
        click(window, u"wordDiffButton"_s);
        QTRY_COMPARE(changed->property("markedChanges").toInt(), 0);
        click(window, u"wordDiffButton"_s);
        QTRY_COMPARE(changed->property("markedChanges").toInt(), 1);

        // Ignoring white space turns line staging off.
        click(window, u"ignoreWhitespaceButton"_s);
        QTRY_VERIFY(repo->ignoreWhitespace());
        QVERIFY(settle(*repo));
        changed = diffLine(window, u"+line 4 changed"_s);
        QVERIFY(changed);
        clickItem(window, changed);
        QCOMPARE(repo->diff()->selectedCount(), 0);
        screenshot(window, u"ignoring-whitespace"_s);
        click(window, u"ignoreWhitespaceButton"_s);
        QTRY_VERIFY(!repo->ignoreWhitespace());
        QVERIFY(settle(*repo));
        changed = diffLine(window, u"+line 4 changed"_s);
        QVERIFY(changed);
        clickItem(window, changed);
        QCOMPARE(repo->diff()->selectedCount(), 1);
        repo->diff()->clearSelection();

        // Every file of the project, for the time-lapse view.
        QVERIFY(QMetaObject::invokeMethod(window, "showFiles"));
        QVERIFY(settle(*repo));
        QTRY_VERIFY(repo->files()->fileCount() >= 3);
        auto *tree = findItem(window->contentItem(), u"fileTree"_s);
        QVERIFY(tree);
        screenshot(window, u"files"_s);
        findItem(window->contentItem(), u"fileFilter"_s)->setProperty("text", u"feature"_s);
        auto *matches = findItem(window->contentItem(), u"fileMatches"_s);
        QTRY_VERIFY(matches && matches->property("count").toInt() == 1);
        QQuickItem *match = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(matches, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, match), Q_ARG(int, 0))
                    && match);
        // A click shows the file's history below the graph, which shows
        // the commits that changed it and selects the one on display
        clickItem(window, match);
        QQuickItem *fileHistory = nullptr;
        QTRY_VERIFY((fileHistory = findItem(window->contentItem(), u"historyTimelapse"_s)));
        auto *fileLapse = window->findChild<Timelapse *>();
        QVERIFY(fileLapse);
        QTRY_VERIFY(fileLapse->revisionCount() >= 1 && !fileLapse->busy());
        QCOMPARE(fileLapse->path(), u"feature.txt"_s);
        QVERIFY(repo->commits()->focused());
        QCOMPARE(repo->commits()->matchCount(), fileLapse->revisionCount());
        QVERIFY(settle(*repo));
        const QString newest = fileLapse->revisions().last().toMap().value(u"sha"_s).toString();
        QTRY_COMPARE(repo->diffCommit(), newest);
        QCOMPARE(window->findChild<QQuickItem *>(u"historyList"_s)->property("currentIndex").toInt(),
                 repo->commits()->rowOf(newest));
        screenshot(window, u"file-history"_s);
        // Out into a window of its own, which leaves the diff below the graph
        click(window, u"timelapsePopOut"_s);
        QQuickWindow *featureLapse = nullptr;
        QTRY_VERIFY((featureLapse = timelapseWindow()));
        QVERIFY(featureLapse->title().contains("feature.txt"_L1));
        QTRY_VERIFY(!findItem(window->contentItem(), u"historyTimelapse"_s));
        QVERIFY(!repo->commits()->focused());
        featureLapse->close();
        QTRY_VERIFY(!timelapseWindow());
        findItem(window->contentItem(), u"fileFilter"_s)->setProperty("text", QString());
        QVERIFY(QMetaObject::invokeMethod(window, "showChanges"));

        // Settings dim the window behind them.
        QVERIFY(QMetaObject::invokeMethod(window, "openSettings"));
        QTest::qWait(100);
        screenshot(window, u"settings"_s);
        QVERIFY(QMetaObject::invokeMethod(window, "closeSettings"));

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
        // Clicking the start of the timeline goes to the first revision.
        {
            auto *timeline = findItem(lapseWindow->contentItem(), u"revisionTimeline"_s);
            QVERIFY(timeline);
            QTest::mouseClick(lapseWindow, Qt::LeftButton, {},
                              timeline->mapToScene({2, timeline->height() / 2}).toPoint());
            QTRY_COMPARE(lapse->revision(), 0);
        }
        // The flow of the file: a node per revision; clicking the newest
        // one shows it
        {
            clickItem(lapseWindow, findItem(lapseWindow->contentItem(), u"showFlow"_s));
            QTRY_COMPARE(lapse->flow().size(), lapse->revisionCount());
            const QVariantMap newest = lapse->flow().last().toMap();
            QVERIFY(newest.value(u"lines"_s).toInt() >= 30);
            auto *flow = findItem(lapseWindow->contentItem(), u"fileFlow"_s);
            QVERIFY(flow);
            QVERIFY(QQuickTest::qWaitForPolish(lapseWindow));
            qreal x = 0;
            QVERIFY(QMetaObject::invokeMethod(flow, "xOf", Q_RETURN_ARG(qreal, x), Q_ARG(int, lapse->revisionCount() - 1)));
            QTest::mouseClick(lapseWindow, Qt::LeftButton, {}, flow->mapToScene({x, flow->height() / 2}).toPoint());
            QTRY_COMPARE(lapse->revision(), lapse->revisionCount() - 1);
            QTRY_VERIFY(!lapse->busy());
            // Clicking low on the current node, or on the minimap, scrolls
            // the code there; the node's marker follows
            auto *lines = findItem(lapseWindow->contentItem(), u"timelapseLines"_s);
            QVERIFY(lines);
            QTRY_VERIFY(flow->property("viewEnd").toReal() > flow->property("viewStart").toReal());
            QCOMPARE(flow->property("viewStart").toReal(), 0.0);
            QVERIFY(QQuickTest::qWaitForPolish(lapseWindow));
            QTest::mouseClick(lapseWindow, Qt::LeftButton, {}, flow->mapToScene({x, flow->height() - 24}).toPoint());
            QTRY_VERIFY(lines->property("contentY").toReal() > 0);
            QTRY_VERIFY(flow->property("viewStart").toReal() > 0);
            auto *minimap = findItem(lapseWindow->contentItem(), u"changeMinimap"_s);
            QVERIFY(minimap);
            QCOMPARE(minimap->property("changes").value<QList<int>>().size(), lapse->rowCount());
            QTest::mouseClick(lapseWindow, Qt::LeftButton, {}, minimap->mapToScene({minimap->width() / 2, 1}).toPoint());
            QTRY_COMPARE(lines->property("contentY").toReal(), lines->property("originY").toReal());
            screenshot(lapseWindow, u"timelapse-flow"_s);
        }
        // Like dragging the slider and toggling, then closing mid-load.
        for (int r = 0; r < lapse->revisionCount(); ++r) {
            lapse->setRevision(r);
            QTest::qWait(5);
        }
        lapse->setShowDeleted(false);
        QTest::qWait(30);
        lapse->setShowDeleted(true);
        lapseWindow->close();
        QTRY_VERIFY(!timelapseWindow());
        QTest::qWait(300);

        // Searching dims what doesn't match; the minimap is above the graph.
        QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));
        auto *search = window->findChild<QQuickItem *>(u"historySearch"_s);
        QVERIFY(search);
        search->setProperty("text", u"message:line author:ada"_s);
        QTRY_COMPARE(repo->commits()->matchCount(), 5);
        QVERIFY(window->findChild<QQuickItem *>(u"minimap"_s)->isVisible());
        screenshot(window, u"search"_s);
        search->setProperty("text", QString());
        QVERIFY(!repo->commits()->searching());
        QVERIFY(QMetaObject::invokeMethod(window, "showChanges"));

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
        // Not only the first key in the list: each row keeps the key picked.
        QVERIFY(QMetaObject::invokeMethod(box, "addTrailer", Q_ARG(QString, u"Task-number"_s)));
        QVERIFY(QMetaObject::invokeMethod(box, "addTrailer", Q_ARG(QString, u"Fixes"_s)));
        QTRY_COMPARE(findItems(window->contentItem(), u"trailerValue"_s).size(), 2);
        findItems(window->contentItem(), u"trailerValue"_s)[0]->setProperty("text", u"QTBUG-123"_s);
        findItems(window->contentItem(), u"trailerValue"_s)[1]->setProperty("text", u"QTBUG-99"_s);
        screenshot(window, u"commit-form"_s);
        click(window, u"commitButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(gitOut({"log", "-1", "--format=%B"}).trimmed(),
                 "Change line four\n\n"
                 "This explains at length why line four had to change, far beyond\n"
                 "seventy-two characters.\n\n"
                 "Task-number: QTBUG-123\n"
                 "Fixes: QTBUG-99"_ba);
        QCOMPARE(window->findChild<QQuickItem *>(u"commitTitle"_s)->property("text").toString(), QString());

        // A trailer's key can be picked from the list or typed.
        {
            QVERIFY(QMetaObject::invokeMethod(box, "addTrailer", Q_ARG(QString, u"Fixes"_s)));
            QVERIFY(QMetaObject::invokeMethod(box, "addTrailer", Q_ARG(QString, u"Fixes"_s)));
            QTRY_COMPARE(findItems(window->contentItem(), u"trailerKey"_s).size(), 2);
            const QList<QQuickItem *> keys = findItems(window->contentItem(), u"trailerKey"_s);
            const QList<QQuickItem *> values = findItems(window->contentItem(), u"trailerValue"_s);
            int pickTo = -1;
            QVERIFY(QMetaObject::invokeMethod(keys[0], "find", Q_RETURN_ARG(int, pickTo), Q_ARG(QString, u"Pick-to"_s)));
            QVERIFY(pickTo > 0);
            QVERIFY(QMetaObject::invokeMethod(keys[0], "activated", Q_ARG(int, pickTo)));
            // Adding a trailer focuses its value field; then edit the key.
            QTRY_VERIFY(values[1]->hasActiveFocus());
            QVERIFY(QQuickTest::qWaitForPolish(window));
            QTest::mouseClick(window, Qt::LeftButton, {}, keys[1]->mapToScene({20, keys[1]->height() / 2}).toPoint());
            QTRY_VERIFY(keys[1]->hasActiveFocus());
            QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
            for (const char c : "Reviewed-by"_ba)
                QTest::keyClick(window, c);
            values[0]->setProperty("text", u"6.8"_s);
            values[1]->setProperty("text", u"Ada"_s);
            QString message;
            QVERIFY(QMetaObject::invokeMethod(box, "message", Q_RETURN_ARG(QString, message)));
            QVERIFY2(message.endsWith(u"Pick-to: 6.8\nReviewed-by: Ada\n"_s), qPrintable(message));
            QVERIFY(QMetaObject::invokeMethod(box, "clear"));
        }

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

        // The divider between the unstaged and the staged files moves, both ways.
        {
            auto *stagedList = window->findChild<QQuickItem *>(u"stagedList"_s);
            QQuickItem *stagedPane = stagedList->parentItem()->parentItem(); // FileList
            const auto drag = [&](int dy) {
                // The middle of the 7 px handle above the staged files.
                const QPoint from = stagedPane->mapToScene({stagedPane->width() / 2, -4}).toPoint();
                QTest::mousePress(window, Qt::LeftButton, {}, from);
                for (int i = 1; i <= 6; ++i)
                    QTest::mouseMove(window, from + QPoint(0, dy * i / 6));
                QTest::mouseRelease(window, Qt::LeftButton, {}, from + QPoint(0, dy));
            };
            const qreal before = stagedPane->height();
            drag(40);
            QTRY_VERIFY(stagedPane->height() < before - 30);
            const qreal shrunk = stagedPane->height();
            drag(-30);
            QTRY_VERIFY(stagedPane->height() > shrunk + 20);
        }

        // Push stays off until the merge is done.
        auto *pushButton = window->findChild<QQuickItem *>(u"pushButton"_s);
        QVERIFY(pushButton && !pushButton->isEnabled());
        git({"commit", "-q", "--no-edit"});

        // Push and fetch, against a local "server".
        QTemporaryDir server;
        GitRunner(server.path()).run({"init", "-q", "--bare", "-b", "main", server.path()});
        git({"remote", "add", "origin", server.path()});
        repo->refresh();
        QVERIFY(settle(*repo));
        QVERIFY(repo->pushDescription().contains("track"_L1));
        click(window, u"pushButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(GitRunner(server.path()).run({"rev-parse", "main"}).out, gitOut({"rev-parse", "HEAD"}));
        QCOMPARE(repo->upstream(), u"origin/main"_s);
        click(window, u"fetchButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(repo->lastFetch().isValid());
        screenshot(window, u"pushed"_s);

        // Push for review with a topic, from the options popup.
        git({"config", "--add", "remote.origin.push", "refs/heads/*:refs/for/*"});
        git({"commit", "-q", "--allow-empty", "-m", "For review"});
        repo->refresh();
        QVERIFY(settle(*repo));
        QVERIFY(repo->pushForReview());
        click(window, u"reviewOptionsButton"_s);
        auto *topic = window->findChild<QQuickItem *>(u"reviewTopicField"_s);
        QTRY_VERIFY(topic && topic->isVisible());
        topic->setProperty("text", u"speedup"_s);
        screenshot(window, u"review-options"_s);
        click(window, u"reviewPushButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(GitRunner(server.path()).run({"rev-parse", "refs/for/main%topic=speedup"}).out,
                 gitOut({"rev-parse", "HEAD"}));
        QCOMPARE(repo->reviewTopic(), u"speedup"_s);

        // GitLab: merge request push options from the same popup. The
        // server is a local path, so there is nothing to ask glab.
        GitRunner(server.path()).run({"config", "receive.advertisePushOptions", "true"});
        git({"config", "--unset-all", "remote.origin.push"});
        repo->setPushMode(u"gitlab"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->reviewHost(), u"gitlab"_s);
        QVERIFY(!repo->pushForReview());
        click(window, u"reviewOptionsButton"_s);
        auto *title = window->findChild<QQuickItem *>(u"reviewTitleField"_s);
        QTRY_VERIFY(title && title->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>(u"reviewTopicField"_s)->isVisible());
        QVERIFY(window->findChild<QQuickItem *>(u"reviewCreateBox"_s)->property("checked").toBool());
        title->setProperty("text", u"Speed up"_s);
        screenshot(window, u"merge-request-options"_s);
        click(window, u"reviewPushButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(GitRunner(server.path()).run({"rev-parse", "main"}).out, gitOut({"rev-parse", "HEAD"}));
        repo->setPushMode(u"auto"_s);
        QVERIFY(settle(*repo));

        // GitHub: the branch's pull request on its tip, from a stand-in
        // for gh. git pushes to the local server instead. The stand-in is a
        // shell script, which Windows can't start as a program.
#ifndef Q_OS_WIN
        {
            QTemporaryDir bin;
            QFile gh(bin.filePath(u"gh"_s));
            QVERIFY(gh.open(QIODevice::WriteOnly));
            gh.write("#!/bin/sh\necho '{\"data\":{\"repository\":{\"pullRequests\":{\"nodes\":[{\"number\":12,"
                     "\"title\":\"Speed up\",\"url\":\"https://github.com/acme/widget/pull/12\",\"state\":\"OPEN\","
                     "\"reviewDecision\":\"APPROVED\",\"headRepositoryOwner\":{\"login\":\"acme\"},"
                     "\"reviewThreads\":{\"nodes\":[{\"isResolved\":false}]}}]}}}}'\n");
            gh.close();
            QVERIFY(gh.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
            const QByteArray path = qgetenv("PATH");
            qputenv("PATH", bin.path().toUtf8() + ':' + path);
            const QString url = u"https://github.com/acme/widget.git"_s;
            git({"config", "url." + server.path() + ".insteadOf", url});
            git({"remote", "set-url", "origin", url});
            repo->refresh();
            QVERIFY(settle(*repo));
            QCOMPARE(repo->reviewHost(), u"github"_s);
            QTRY_VERIFY(!repo->branchReview().isEmpty());
            QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));
            QQuickItem *tag = nullptr;
            QTRY_VERIFY((tag = [window] {
                const QList<QQuickItem *> tags = findItems(window->contentItem(), u"reviewTag"_s);
                const auto it = std::find_if(tags.cbegin(), tags.cend(), [](QQuickItem *t) { return t->isVisible(); });
                return it == tags.cend() ? nullptr : *it;
            }()));
            QCOMPARE(tag->property("text").toString(), u"PR #12 · approved · 1 unresolved"_s);
            screenshot(window, u"pull-request"_s);

            git({"remote", "set-url", "origin", server.path()});
            repo->refresh();
            QVERIFY(settle(*repo));
            QCOMPARE(repo->reviewHost(), QString());
            qputenv("PATH", path);
        }
#endif

        for (const QQmlError &w : std::as_const(warnings))
            qWarning().noquote() << w.toString();
        QVERIFY(warnings.isEmpty());

        // A time-lapse window survives garbage collection, and closes with
        // the main window.
        QVERIFY(QMetaObject::invokeMethod(window, "openTimelapse", Q_ARG(QString, u"app.txt"_s)));
        QTRY_VERIFY(timelapseWindow() && !timelapseWindow()->findChild<Timelapse *>()->busy()
                    && timelapseWindow()->findChild<Timelapse *>()->rowCount() > 0);
        engine.collectGarbage();
        QTest::qWait(200);
        QVERIFY(timelapseWindow());
        window->close();
        QTRY_VERIFY(!timelapseWindow());
        QVERIFY(warnings.isEmpty());
    }

    // The Review section of a commit pushed for review, against a Gerrit
    // made of a bare repository and an ssh stand-in that answers queries.
    void showsGerritReviews()
    {
#ifdef Q_OS_WIN
        QSKIP("The ssh stand-in is a shell script, which Windows can't start as a program.");
#endif
        QTemporaryDir root;
        const QString remote = root.filePath(u"remote.git"_s);
        const QString url = u"ssh://ada@review.example:29418/demo"_s;
        QStringList failures;
        const auto run = [&failures](const QString &dir, const QStringList &args) {
            const GitResult result = GitRunner(dir).run(args);
            if (!result.ok())
                failures << result.errorText();
            return result.out.trimmed();
        };
        const auto writeFile = [](const QString &path, const QByteArray &content) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(content);
        };
        const QString work = root.filePath(u"work"_s);
        const QString other = root.filePath(u"other"_s);
        const QString source = u"/parser.cpp"_s;
        const QByteArray base = "int parse(const char *text)\n{\n    return 0;\n}\n";
        const QByteArray changeId = "\n\nChange-Id: I0123456789abcdef0123456789abcdef01234567\n";
        run(root.path(), {"init", "-q", "--bare", "-b", "main", remote});
        run(root.path(), {"init", "-q", "-b", "main", work});
        writeFile(work + source, base);
        run(work, {"add", "."});
        run(work, {"commit", "-q", "-m", "Add a parser"});
        run(work, {"remote", "add", "origin", remote});
        run(work, {"push", "-q", "-u", "origin", "main"});
        run(work, {"remote", "set-url", "origin", url});
        run(work, {"config", "url." + remote + ".insteadOf", url});
        // Patch set 1 was uploaded from another clone.
        run(root.path(), {"clone", "-q", remote, other});
        writeFile(other + source, "int parse(const char *text)\n{\n    if (!text)\n        return -1;\n"
                                  "    return 0;\n}\n");
        run(other, {"commit", "-q", "-a", "-m", "Parse nothing gracefully" + changeId});
        run(other, {"push", "-q", "origin", "HEAD:refs/changes/42/1042/1"});
        const QByteArray firstTry = run(other, {"rev-parse", "HEAD"});
        writeFile(work + source, "int parse(const char *text)\n{\n    if (!text || !*text)\n"
                                 "        return -1; // nothing to parse\n    return 0;\n}\n");
        run(work, {"commit", "-q", "-a", "-m", "Parse nothing gracefully" + changeId});
        const QByteArray head = run(work, {"rev-parse", "HEAD"});
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(u'\n')));

        QTemporaryDir bin;
        writeFile(bin.filePath(u"ssh"_s), "#!/bin/sh\ncase \"$*\" in *has:unresolved*) cat \"$FAKE_GERRIT_JSON.unresolved\";;\n"
                                          "*) cat \"$FAKE_GERRIT_JSON\";; esac\n");
        QVERIFY(QFile::setPermissions(bin.filePath(u"ssh"_s), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QString json = bin.filePath(u"answer.json"_s);
        writeFile(json, R"json({"id":"I0123456789abcdef0123456789abcdef01234567","number":1042,)json"
                        R"json("url":"https://review.example/c/demo/+/1042","status":"NEW",)json"
                        R"json("comments":[{"timestamp":1760000000,"reviewer":{"name":"Ada Lovelace"},"message":"Uploaded patch set 1."},)json"
                        R"json({"timestamp":1760003600,"reviewer":{"name":"Charles Babbage"},)json"
                        R"json("message":"Patch Set 1: Code-Review-1\n\n(2 comments)\n\nAn empty string is nothing to parse either."},)json"
                        R"json({"timestamp":1760090000,"reviewer":{"name":"Ada Lovelace"},"message":"Uploaded patch set 2."},)json"
                        R"json({"timestamp":1760093600,"reviewer":{"name":"Qt CI Bot"},"message":"Patch Set 2: Verified+1\n\nBuild passed."}],)json"
                        R"json("currentPatchSet":{"number":2,"revision":")json" + head + R"json(",)json"
                        R"json("approvals":[{"type":"Verified","value":"1","by":{"name":"Qt CI Bot"}}],)json"
                        R"json("comments":[{"file":"parser.cpp","line":4,"reviewer":{"name":"Charles Babbage"},)json"
                        R"json("message":"Say what -1 means in the header, please."}]},)json"
                        R"json("patchSets":[{"number":1,"revision":")json" + firstTry + R"json(","ref":"refs/changes/42/1042/1",)json"
                        R"json("uploader":{"name":"Ada Lovelace"},"createdOn":1760000000,"kind":"REWORK","comments":[)json"
                        R"json({"file":"parser.cpp","line":3,"reviewer":{"name":"Charles Babbage"},"message":"What about an empty string?"},)json"
                        R"json({"file":"parser.cpp","reviewer":{"name":"Charles Babbage"},"message":"Needs a test."}]},)json"
                        R"json({"number":2,"revision":")json" + head + R"json(","ref":"refs/changes/42/1042/2",)json"
                        R"json("uploader":{"name":"Ada Lovelace"},"createdOn":1760090000,"kind":"REWORK","comments":[)json"
                        R"json({"file":"parser.cpp","line":4,"reviewer":{"name":"Charles Babbage"},)json"
                        R"json("message":"Say what -1 means in the header, please."}]}]})json"
                        "\n" R"json({"type":"stats","rowCount":1})json" "\n");
        writeFile(json + u".unresolved"_s, R"json({"id":"I0123456789abcdef0123456789abcdef01234567","number":1042})json" "\n");
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", bin.path().toUtf8() + ':' + path);
        qputenv("FAKE_GERRIT_JSON", json.toUtf8());

        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &w) { warnings += w; });
        engine.setInitialProperties({{u"initialPath"_s, work}});
        engine.loadFromModule("Igitt", "Main");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window && QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(repo && settle(*repo));
        QTRY_COMPARE(repo->commits()->data(repo->commits()->index(0), CommitModel::ReviewRole).toMap()
                         .value(u"number"_s).toInt(), 1042);

        repo->showCommit(QString::fromLatin1(head));
        QVERIFY(settle(*repo));
        auto *panel = window->findChild<QQuickItem *>(u"reviewPanel"_s);
        QTRY_VERIFY(panel && panel->isVisible());
        QTest::qWait(50);
        screenshot(window, u"review"_s);

        click(window, u"messagesTab"_s);
        QTest::qWait(50);
        screenshot(window, u"review-messages"_s);
        click(window, u"patchSetsTab"_s);
        QTest::qWait(50);
        screenshot(window, u"review-patch-sets"_s);
        QQuickItem *compare = nullptr;
        QTRY_VERIFY((compare = findItem(window->contentItem(), u"comparePatchSet1"_s)));
        QVERIFY(!findItem(window->contentItem(), u"comparePatchSet2"_s)->isEnabled()); // this commit
        clickItem(window, compare);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->diffSource(), Repository::PatchSetDiff);
        QCOMPARE(repo->diffPatchSet(), 1);
        QVERIFY(repo->diff()->rowCount() > 0);
        QTest::qWait(50);
        screenshot(window, u"review-compare"_s);
        click(window, u"showCommitButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->diffSource(), Repository::CommitDiff);

        qputenv("PATH", path);
        for (const QQmlError &w : std::as_const(warnings))
            qWarning().noquote() << w.toString();
        QVERIFY(warnings.isEmpty());
    }

    // While a modal dialog is open, nothing behind it takes the pointer:
    // not where the dialog has nothing of its own to press, and not beside
    // it. No row is dragged or chosen.
    // The window's modes: History to explore, Changes to commit, Compose to
    // rework the commits not pushed yet; each shows its own diff.
    void switchesModes()
    {
        // A commit not pushed yet and a change of its own, whatever the
        // tests before left
        write(u"modes.txt"_s, "one\n");
        git({"add", "modes.txt"});
        git({"commit", "-q", "-m", "Try the modes", "--", "modes.txt"}); // that file only, whatever else is staged
        write(u"modes.txt"_s, "one\ntwo\n");
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        enum { History, Changes, Compose };
        const auto mode = [window] { return window->property("mode").toInt(); };
        // With changes, it opens on them
        QTRY_COMPARE(mode(), int(Changes));
        auto *changesButton = window->findChild<QQuickItem *>(u"changesModeButton"_s);
        QVERIFY(changesButton->property("text").toString().contains(QRegularExpression(u"\\d"_s))); // how many
        QVERIFY(window->findChild<QQuickItem *>(u"commitBox"_s)->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>(u"historyList"_s)->isVisible());

        // Above the changes, every commit not pushed yet and the one they
        // stand on
        auto *recent = window->findChild<QQuickItem *>(u"recentCommits"_s);
        QTRY_VERIFY(recent && recent->isVisible());
        const int unpushed = gitOut({"rev-list", "--count", "HEAD", "--not", "--remotes"}).trimmed().toInt();
        QVERIFY(unpushed >= 1);
        const int commits = gitOut({"rev-list", "--count", "HEAD"}).trimmed().toInt();
        QTRY_COMPARE(recent->property("count").toInt(), std::min(unpushed + 1, commits));

        // A file dropped onto one folds into it, all its lines
        {
            QQuickItem *file = nullptr;
            QTRY_VERIFY((file = fileRow(window, u"unstagedList"_s, u"modes.txt"_s)));
            QQuickItem *head = nullptr;
            QTRY_VERIFY(QMetaObject::invokeMethod(recent, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, head), Q_ARG(int, 0))
                        && head);
            QVERIFY(QQuickTest::qWaitForPolish(window));
            // Over the commit, the label under the pointer says it folds
            const QPoint from = file->mapToScene({file->width() / 2, file->height() / 2}).toPoint();
            const QPoint to = head->mapToScene({head->width() / 3, head->height() / 2}).toPoint();
            QTest::mousePress(window, Qt::LeftButton, {}, from);
            for (int i = 1; i <= 12; ++i)
                QTest::mouseMove(window, from + (to - from) * i / 12);
            // On the overlay, under the window's root item
            QQuickItem *label = nullptr;
            QTRY_VERIFY((label = findItem(window->contentItem(), u"fileDragLabel"_s)));
            QCOMPARE(label->property("text").toString(), u"Fold modes.txt into “Try the modes”"_s);
            // Over the pushed one below, why not
            if (recent->property("count").toInt() > unpushed) {
                QQuickItem *pushed = nullptr;
                QVERIFY(QMetaObject::invokeMethod(recent, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, pushed),
                                                  Q_ARG(int, unpushed)));
                QVERIFY(pushed);
                const QPoint over = pushed->mapToScene({pushed->width() / 3, pushed->height() / 2}).toPoint();
                for (int i = 1; i <= 4; ++i)
                    QTest::mouseMove(window, to + (over - to) * i / 4);
                QTRY_COMPARE(label->property("text").toString(),
                             u"Can't fold modes.txt: it's pushed; folding into it would need a force push"_s);
                for (int i = 1; i <= 4; ++i)
                    QTest::mouseMove(window, over + (to - over) * i / 4);
                QTRY_COMPARE(label->property("text").toString(), u"Fold modes.txt into “Try the modes”"_s);
            }
            QTest::mouseRelease(window, Qt::LeftButton, {}, to);
            QVERIFY(settle(*repo));
            QTRY_COMPARE(gitOut({"show", "HEAD:modes.txt"}), "one\ntwo\n"_ba);
            QCOMPARE(gitOut({"log", "-1", "--format=%s"}), "Try the modes\n"_ba);
            QCOMPARE(gitOut({"show", "--name-only", "--format=", "HEAD"}), "modes.txt\n"_ba); // that file only
            QVERIFY(!gitOut({"status", "--porcelain"}).contains("modes.txt"));
            repo->undoFold();
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"show", "HEAD:modes.txt"}), "one\n"_ba);
        }
        // A commit of the strip is shown in History
        QQuickItem *first = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(recent, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, first), Q_ARG(int, 0))
                    && first);
        clickItem(window, first);
        QTRY_COMPARE(mode(), int(History));
        QVERIFY(settle(*repo));
        QCOMPARE(repo->diffSource(), Repository::CommitDiff);
        QCOMPARE(repo->diffCommit(), repo->commits()->shaAt(0));
        QVERIFY(window->findChild<QQuickItem *>(u"historyList"_s)->isVisible());
        QVERIFY(window->findChild<QQuickItem *>(u"fileTree"_s)->isVisible());

        // Back in Changes, its diff is the working tree's again; in
        // History, the commit shown before
        QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
        QTRY_COMPARE(mode(), int(Changes));
        QVERIFY(settle(*repo));
        QVERIFY(repo->diffSource() == Repository::UnstagedDiff || repo->diffSource() == Repository::StagedDiff);
        QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
        QTRY_COMPARE(mode(), int(History));
        QVERIFY(settle(*repo));
        QCOMPARE(repo->diffCommit(), repo->commits()->shaAt(0));

        // The uncommitted changes on top of the graph lead to Changes
        auto *list = window->findChild<QQuickItem *>(u"historyList"_s);
        QQuickItem *wip = list->property("headerItem").value<QQuickItem *>();
        QVERIFY(wip && wip->isVisible());
        clickItem(window, wip);
        QTRY_COMPARE(mode(), int(Changes));

        // Compose reads the changes in the window itself
        QVERIFY(!window->findChild<ChangeClusters *>());
        QTest::keyClick(window, Qt::Key_3, Qt::ControlModifier);
        QTRY_COMPARE(mode(), int(Compose));
        ChangeClusters *clusters = nullptr;
        QTRY_VERIFY((clusters = window->findChild<ChangeClusters *>()));
        QTRY_VERIFY(clusters->pieceCount() > 0);
        QVERIFY(window->findChild<QQuickItem *>(u"groupsView"_s)->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>(u"composePlanButton"_s)->isVisible());
        // The commit and the file gone, what was staged before still staged
        git({"reset", "-q", "--soft", "HEAD~1"});
        git({"rm", "-q", "--cached", "-f", "modes.txt"});
        QFile::remove(m_dir.filePath(u"modes.txt"_s));
    }

    // History shows a file's history below the graph, and the two go along:
    // a revision picked selects its commit, a commit picked shows the file
    // as it was then.
    void showsAFilesHistoryInHistory()
    {
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QVERIFY(QMetaObject::invokeMethod(window, "showFileHistory", Q_ARG(QString, u"app.txt"_s)));
        QTRY_COMPARE(window->property("mode").toInt(), 0);
        Timelapse *lapse = nullptr;
        QTRY_VERIFY((lapse = window->findChild<Timelapse *>()));
        QTRY_VERIFY(lapse->revisionCount() >= 3 && !lapse->busy());
        QVERIFY(settle(*repo));
        const auto shaOf = [lapse](int revision) {
            return lapse->revisions().value(revision).toMap().value(u"sha"_s).toString();
        };
        auto *list = window->findChild<QQuickItem *>(u"historyList"_s);
        QCOMPARE(repo->commits()->matchCount(), lapse->revisionCount());

        // Going to the revision before keeps the code in sight where it was,
        // not at the start
        {
            auto *code = findItem(window->contentItem(), u"timelapseLines"_s);
            QVERIFY(code);
            const qreal rowHeight = code->property("rowHeight").toReal();
            QTRY_VERIFY(code->property("contentHeight").toReal() > code->height() + 10 * rowHeight);
            code->setProperty("contentY", code->property("originY").toReal() + 12 * rowHeight);
            QTest::qWait(50);
            const int from = lapse->revision();
            const qreal top = lapse->lineAt(code->property("firstRow").toReal());
            QVERIFY(top > 5);
            lapse->setRevision(from - 1);
            QTRY_VERIFY(!lapse->busy() && code->property("shownRevision").toInt() == from - 1);
            const qreal expected = lapse->mapLine(from, from - 1, top);
            QTRY_VERIFY2(std::abs(lapse->lineAt(code->property("firstRow").toReal()) - expected) < 1,
                         qPrintable(u"top line %1, expected %2"_s.arg(lapse->lineAt(code->property("firstRow").toReal())).arg(expected)));
            QVERIFY(code->property("contentY").toReal() > code->property("originY").toReal());
            QVERIFY(settle(*repo));
        }

        // The oldest revision picked: its commit in the graph
        lapse->setRevision(0);
        QVERIFY(settle(*repo));
        QTRY_COMPARE(repo->diffCommit(), shaOf(0));
        QCOMPARE(list->property("currentIndex").toInt(), repo->commits()->rowOf(shaOf(0)));

        // One of the file's commits clicked in the graph: that revision
        QQuickItem *row = historyRow(window, repo->commits()->rowOf(shaOf(1)));
        QVERIFY(row);
        clickItem(window, row);
        QVERIFY(settle(*repo));
        QTRY_COMPARE(lapse->revision(), 1);

        // A commit that left the file alone: it stays picked, and the file
        // is shown as the newest revision below it left it
        QStringList shas;
        for (int i = 0; i < lapse->revisionCount(); ++i)
            shas.append(shaOf(i));
        int other = 0;
        while (other < repo->commits()->rowCount() && shas.contains(repo->commits()->shaAt(other)))
            ++other;
        QVERIFY(other < repo->commits()->rowCount());
        int expected = -1;
        int expectedRow = INT_MAX;
        for (int i = 0; i < shas.size(); ++i) {
            const int r = repo->commits()->rowOf(shas[i]);
            if (r >= other && r < expectedRow) {
                expected = i;
                expectedRow = r;
            }
        }
        list->setProperty("contentY", 0);
        row = historyRow(window, other);
        QVERIFY(row);
        clickItem(window, row);
        QVERIFY(settle(*repo));
        QTRY_COMPARE(lapse->revision(), expected);
        QTest::qWait(100);
        QCOMPARE(repo->diffCommit(), repo->commits()->shaAt(other));

        // Closed: the diff of that commit again, and every commit
        click(window, u"timelapseClose"_s);
        QTRY_VERIFY(!findItem(window->contentItem(), u"historyTimelapse"_s));
        QVERIFY(!repo->commits()->focused());
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"diffList"_s)->isVisible());
        QCOMPARE(repo->diffCommit(), repo->commits()->shaAt(other));
    }

    // Branches from the window: one made at a commit from its menu, one at
    // HEAD from the switcher in the tool bar and switched to, the switcher
    // going back, both deleted from their labels' menus. The uncommitted
    // changes stay, so the branches go where git can carry them.
    void makesAndSwitchesBranches()
    {
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        const QString original = repo->branch();
        const QString head = repo->headSha();
        QVERIFY(!original.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));
        const QString older = repo->commits()->shaAt(1);

        // At an older commit, staying where it is
        rightClickCommit(window, 1);
        click(window, u"createBranchMenuItem"_s);
        auto *name = window->findChild<QQuickItem *>(u"branchName"_s);
        QTRY_VERIFY(name && name->isVisible());
        name->setProperty("text", u"bad name"_s);
        QTRY_VERIFY(!window->findChild<QQuickItem *>(u"branchOkButton"_s)->isEnabled());
        name->setProperty("text", u"try/older"_s);
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"branchOkButton"_s)->isEnabled());
        click(window, u"branchCheckout"_s);
        click(window, u"branchOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->branch(), original);
        QCOMPARE(gitOut({"rev-parse", "try/older"}).trimmed(), older.toUtf8());

        // At HEAD from the switcher, and switched to
        click(window, u"branchButton"_s);
        click(window, u"newBranchButton"_s);
        QTRY_VERIFY(name->isVisible());
        name->setProperty("text", u"try/here"_s);
        click(window, u"branchOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->branch(), u"try/here"_s);
        QCOMPARE(repo->headSha(), head);

        // Back by the switcher: typed, then Enter
        click(window, u"branchButton"_s);
        auto *filter = window->findChild<QQuickItem *>(u"branchFilter"_s);
        QTRY_VERIFY(filter && filter->hasActiveFocus());
        filter->setProperty("text", original);
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->branch(), original);

        // Deleted from its label's menu, merged as it is
        for (const QString &branch : {u"try/older"_s}) {
            QQuickItem *label = nullptr;
            QTRY_VERIFY((label = [&]() -> QQuickItem * {
                for (QQuickItem *item : findItems(window->contentItem(), u"refLabel"_s)) {
                    if (item->property("name").toString() == branch)
                        return item;
                }
                return nullptr;
            }()));
            QVERIFY(QQuickTest::qWaitForPolish(window));
            QTest::mouseClick(window, Qt::RightButton, {},
                              label->mapToScene({label->width() / 2, label->height() / 2}).toPoint());
            click(window, u"deleteBranchMenuItem"_s);
            confirmDelete(window, branch);
            QVERIFY(settle(*repo));
            QVERIFY2(!repo->branches().contains(branch), qPrintable(branch));
        }
        // The one at HEAD, behind "+1" in a narrow column: from the switcher
        click(window, u"branchButton"_s);
        auto *branchList = window->findChild<QQuickItem *>(u"branchList"_s);
        QQuickItem *entry = nullptr;
        QTRY_VERIFY((entry = [&]() -> QQuickItem * {
            for (int i = 0; i < branchList->property("count").toInt(); ++i) {
                QQuickItem *item = nullptr;
                QMetaObject::invokeMethod(branchList, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
                if (item && item->property("modelData").toMap().value(u"name"_s) == u"try/here"_s)
                    return item;
            }
            return nullptr;
        }()));
        QVERIFY(QQuickTest::qWaitForPolish(window));
        QTest::mouseClick(window, Qt::RightButton, {}, entry->mapToScene({entry->width() / 2, entry->height() / 2}).toPoint());
        click(window, u"switcherDeleteItem"_s);
        confirmDelete(window, u"try/here"_s);
        QVERIFY(settle(*repo));
        QVERIFY(!repo->branches().contains(u"try/here"_s));
        QCOMPARE(repo->branch(), original);
    }

    // Branches cannot be planned: while planning, the switcher and the
    // branch actions of the menus are off, and say so.
    void keepsBranchesWhilePlanning()
    {
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        const QString branch = repo->branch();
        QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));
        auto *button = window->findChild<QQuickItem *>(u"branchButton"_s);
        QVERIFY(button->isEnabled());
        repo->startPlanning();
        QVERIFY(settle(*repo));
        const auto discard = qScopeGuard([&] {
            repo->discardPlan();
            QVERIFY(settle(*repo));
        });
        QTRY_VERIFY(!button->isEnabled());

        rightClickCommit(window, 1);
        auto *create = window->findChild<QQuickItem *>(u"createBranchMenuItem"_s);
        QTRY_VERIFY(create && create->isVisible());
        QVERIFY(!create->isEnabled());
        QVERIFY(create->property("text").toString().contains(u"planning"_s));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("popupOpen").toBool());

        // A label's menu, and a double-click on it, switch nothing
        QQuickItem *label = nullptr;
        QTRY_VERIFY((label = [&]() -> QQuickItem * {
            for (QQuickItem *item : findItems(window->contentItem(), u"refLabel"_s)) {
                if (item->property("name").toString() != branch && item->property("kind").toString() == u"branch"_s)
                    return item;
            }
            return nullptr;
        }()));
        QVERIFY(QQuickTest::qWaitForPolish(window));
        const QPoint at = label->mapToScene({label->width() / 2, label->height() / 2}).toPoint();
        QTest::mouseClick(window, Qt::RightButton, {}, at);
        for (const QString &item : {u"checkoutMenuItem"_s, u"renameBranchMenuItem"_s, u"deleteBranchMenuItem"_s}) {
            auto *entry = window->findChild<QQuickItem *>(item);
            QTRY_VERIFY2(entry && entry->isVisible(), qPrintable(item));
            QVERIFY2(!entry->isEnabled(), qPrintable(item));
            QVERIFY2(entry->property("text").toString().contains(u"planning"_s), qPrintable(item));
        }
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("popupOpen").toBool());
        QTest::mouseDClick(window, Qt::LeftButton, {}, at);
        QTest::qWait(100);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->branch(), branch);

        // Asked for anyway: refused
        QSignalSpy errors(repo, &Repository::errorOccurred);
        repo->createBranch(u"planned"_s, {}, true);
        QCOMPARE(errors.size(), 1);
        QVERIFY(!repo->branches().contains(u"planned"_s));
    }

    // A merge stopped at a conflict shows a banner to continue or abort it.
    void showsTheOperationInProgress()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        const auto write = [&](const QByteArray &content) {
            QFile file(dir.filePath(u"a.txt"_s));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(content);
        };
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        write("one\n");
        git.run({u"add"_s, u"a.txt"_s});
        git.run({u"commit"_s, u"-q"_s, u"-m"_s, u"One"_s});
        git.run({u"checkout"_s, u"-q"_s, u"-b"_s, u"other"_s});
        write("there\n");
        git.run({u"commit"_s, u"-q"_s, u"-am"_s, u"There"_s});
        git.run({u"checkout"_s, u"-q"_s, u"main"_s});
        write("here\n");
        git.run({u"commit"_s, u"-q"_s, u"-am"_s, u"Here"_s});
        QVERIFY(!git.run({u"merge"_s, u"other"_s}).ok());

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QCOMPARE(repo->operation(), u"Merging"_s);
        auto *banner = window->findChild<QQuickItem *>(u"operationBanner"_s);
        QTRY_VERIFY(banner && banner->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>(u"pullButton"_s)->isEnabled());
        click(window, u"abortOperationButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->operation(), QString());
        QTRY_VERIFY(!banner->isVisible());
    }

    // Changes thrown away after a guard, from a file's menu or the diff's
    // selection; stashed from the tool bar, and a stash dropped after one.
    // A file's changes stashed from its menu, the rest left
    void stashesAFile()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        const auto write = [&](const QString &name, const QByteArray &content) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(content);
        };
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        write(u"a.txt"_s, "a\n");
        write(u"b.txt"_s, "b\n");
        git.run({u"add"_s, u"."_s});
        git.run({u"commit"_s, u"-q"_s, u"-m"_s, u"One"_s});
        write(u"a.txt"_s, "a changed\n");
        write(u"b.txt"_s, "b changed\n");

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QTRY_COMPARE(window->property("mode").toInt(), 1);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = fileRow(window, u"unstagedList"_s, u"a.txt"_s)));
        QTest::mouseClick(window, Qt::RightButton, {}, row->mapToScene({row->width() / 2, row->height() / 2}).toPoint());
        click(window, u"stashFileMenuItem"_s);
        auto *dialog = window->findChild<QObject *>(u"stashDialog"_s);
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("paths").toStringList(), QStringList{u"a.txt"_s});
        QVERIFY(!window->findChild<QQuickItem *>(u"stashStaged"_s)->isEnabled()); // nothing staged
        click(window, u"stashOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(git.run({u"status"_s, u"--porcelain"_s}).out, " M b.txt\n"_ba);
        QVERIFY(git.run({u"stash"_s, u"show"_s, u"--name-only"_s}).out.contains("a.txt"));
    }

    // A branch pushed under another name, from its dialog
    void pushesABranchElsewhere()
    {
        QTemporaryDir dir;
        QTemporaryDir remote;
        GitRunner git(dir.path());
        GitRunner(remote.path()).run({u"init"_s, u"-q"_s, u"--bare"_s});
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        git.run({u"commit"_s, u"-q"_s, u"--allow-empty"_s, u"-m"_s, u"Start"_s});
        git.run({u"remote"_s, u"add"_s, u"origin"_s, remote.path()});
        git.run({u"branch"_s, u"feature"_s});

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        auto *dialog = window->findChild<QObject *>(u"pushBranchDialog"_s);
        QMetaObject::invokeMethod(dialog, "ask", Q_ARG(QString, u"feature"_s));
        QTRY_VERIFY(dialog->property("visible").toBool());
        auto *name = findItem(window->contentItem(), u"pushRemoteBranch"_s);
        QCOMPARE(name->property("text").toString(), u"feature"_s);
        QCOMPARE(findItem(window->contentItem(), u"pushRemote"_s)->property("currentText").toString(), u"origin"_s);
        QVERIFY(findItem(window->contentItem(), u"pushTrack"_s)->property("checked").toBool());
        name->setProperty("text", u"bad name"_s);
        QTRY_VERIFY(!window->findChild<QQuickItem *>(u"pushBranchOkButton"_s)->isEnabled());
        name->setProperty("text", u"review/feature"_s);
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"pushBranchOkButton"_s)->isEnabled());
        click(window, u"pushBranchOkButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(GitRunner(remote.path()).run({u"rev-parse"_s, u"--verify"_s, u"refs/heads/review/feature"_s}).ok());
    }

    // A merge asks how, and remembers it
    void mergesAsChosen()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        git.run({u"commit"_s, u"-q"_s, u"--allow-empty"_s, u"-m"_s, u"Start"_s});
        git.run({u"checkout"_s, u"-q"_s, u"-b"_s, u"feature"_s});
        git.run({u"commit"_s, u"-q"_s, u"--allow-empty"_s, u"-m"_s, u"Feature"_s});
        git.run({u"checkout"_s, u"-q"_s, u"main"_s});

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        auto *dialog = window->findChild<QObject *>(u"mergeDialog"_s);
        QMetaObject::invokeMethod(dialog, "ask", Q_ARG(QString, u"feature"_s));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("how").toString(), QString());
        click(window, u"mergeNoFf"_s);
        QCOMPARE(dialog->property("how").toString(), u"no-ff"_s);
        click(window, u"mergeOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(git.run({u"log"_s, u"-1"_s, u"--format=%P"_s}).out.trimmed().split(' ').size(), 2);
        // Asked again, the way chosen last
        QMetaObject::invokeMethod(dialog, "ask", Q_ARG(QString, u"feature"_s));
        QTRY_VERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("how").toString(), u"no-ff"_s);
        QMetaObject::invokeMethod(dialog, "reject");
    }

    // An untracked file, from its menu, into .gitignore
    void ignoresAnUntrackedFile()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        git.run({u"commit"_s, u"-q"_s, u"--allow-empty"_s, u"-m"_s, u"Start"_s});
        QFile log(dir.filePath(u"debug.log"_s));
        QVERIFY(log.open(QIODevice::WriteOnly));
        log.write("noise\n");
        log.close();

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QTRY_COMPARE(window->property("mode").toInt(), 1);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = fileRow(window, u"unstagedList"_s, u"debug.log"_s)));
        QTest::mouseClick(window, Qt::RightButton, {}, row->mapToScene({row->width() / 2, row->height() / 2}).toPoint());
        click(window, u"ignoreFileMenuItem"_s);
        QQuickItem *pattern = nullptr;
        QTRY_VERIFY((pattern = findItem(window->contentItem(), u"ignorePattern"_s)) && pattern->isVisible());
        QCOMPARE(pattern->property("text").toString(), u"/debug.log"_s);
        QCOMPARE(window->findChild<QObject *>(u"ignoreDialog"_s)->property("patterns").toStringList(),
                 (QStringList{u"/debug.log"_s, u"*.log"_s}));
        pattern->setProperty("text", u"*.log"_s);
        click(window, u"ignoreOkButton"_s);
        QVERIFY(settle(*repo));
        QFile ignore(dir.filePath(u".gitignore"_s));
        QVERIFY(ignore.open(QIODevice::ReadOnly));
        QCOMPARE(ignore.readAll(), "*.log\n"_ba);
        QTRY_VERIFY(!fileRow(window, u"unstagedList"_s, u"debug.log"_s));
    }

    // A branch deleted is in the lost work, and shows from there
    void recoversLostWork()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        git.run({u"commit"_s, u"-q"_s, u"--allow-empty"_s, u"-m"_s, u"Start"_s});
        git.run({u"checkout"_s, u"-q"_s, u"-b"_s, u"side"_s});
        git.run({u"commit"_s, u"-q"_s, u"--allow-empty"_s, u"-m"_s, u"Lost on the side"_s});
        const QString lost = QString::fromUtf8(git.run({u"rev-parse"_s, u"HEAD"_s}).out.trimmed());
        git.run({u"checkout"_s, u"-q"_s, u"main"_s});
        git.run({u"branch"_s, u"-q"_s, u"-D"_s, u"side"_s});

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        auto *dialog = window->findChild<QObject *>(u"recoverDialog"_s);
        QVERIFY(dialog);
        QMetaObject::invokeMethod(dialog, "open");
        QVERIFY(settle(*repo));
        QList<QQuickItem *> rows;
        // Only the lost commit; with everything, main's too
        QTRY_COMPARE((rows = findItems(window->contentItem(), u"lostWorkRow"_s)).size(), 1);
        QCOMPARE(rows.first()->property("modelData").toMap().value(u"sha"_s).toString(), lost);
        findItem(window->contentItem(), u"recoverOnlyLost"_s)->setProperty("checked", false);
        QTRY_VERIFY(findItems(window->contentItem(), u"lostWorkRow"_s).size() > 1);
        findItem(window->contentItem(), u"recoverOnlyLost"_s)->setProperty("checked", true);
        QTRY_COMPARE((rows = findItems(window->contentItem(), u"lostWorkRow"_s)).size(), 1);
        clickItem(window, rows.first());
        auto *show = rows.first()->findChild<QQuickItem *>(u"lostShowItem"_s);
        QVERIFY(show);
        QTRY_VERIFY(show->isVisible());
        clickItem(window, show);
        QVERIFY(settle(*repo));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(window->property("mode").toInt(), 0);
        QCOMPARE(repo->diffCommit(), lost);
    }

    // The repositories opened are remembered, latest first; with none
    // open, the start page offers them
    void remembersRepositories()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        QTemporaryDir none;
        for (QTemporaryDir *dir : {&first, &second})
            GitRunner(dir->path()).run({u"init"_s, u"-q"_s});
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, none.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QVERIFY(!repo->valid());
        auto *start = findItem(window->contentItem(), u"startPage"_s);
        QVERIFY(start);
        QTRY_VERIFY(start->isVisible());

        QObject *tools = nullptr;
        for (QObject *child : window->findChildren<QObject *>()) {
            if (child->property("recentLimit").isValid())
                tools = child;
        }
        QVERIFY(tools);
        const auto recent = [tools] {
            QStringList paths;
            for (const QString &path : tools->property("recent").toStringList())
                paths << QFileInfo(path).canonicalFilePath();
            return paths;
        };
        const auto canonical = [](const QTemporaryDir &dir) { return QFileInfo(dir.path()).canonicalFilePath(); };
        QMetaObject::invokeMethod(tools, "forgetAll");
        QMetaObject::invokeMethod(tools, "remember", Q_ARG(QString, first.path()));
        QMetaObject::invokeMethod(tools, "remember", Q_ARG(QString, second.path()));
        QCOMPARE(recent(), (QStringList{canonical(second), canonical(first)}));

        QList<QQuickItem *> rows;
        QTRY_VERIFY((rows = findItems(window->contentItem(), u"recentRepository"_s)).size() == 2);
        clickItem(window, rows.last());
        QVERIFY(settle(*repo));
        QCOMPARE(QFileInfo(repo->path()).canonicalFilePath(), canonical(first));
        QTRY_VERIFY(!start->isVisible());
        QTRY_COMPARE(recent(), (QStringList{canonical(first), canonical(second)}));

        repo->open(second.path());
        QVERIFY(settle(*repo));
        QTRY_COMPARE(recent(), (QStringList{canonical(second), canonical(first)}));
        QMetaObject::invokeMethod(tools, "forget", Q_ARG(QString, tools->property("recent").toStringList().last()));
        QCOMPARE(recent(), QStringList{canonical(second)});
        QMetaObject::invokeMethod(tools, "forgetAll");
    }

    // What git asks shows in a dialog, a password hidden, one at a time
    void asksForCredentials()
    {
        QTemporaryDir dir;
        GitRunner(dir.path()).run({u"init"_s, u"-q"_s});
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(settle(*window->findChild<Repository *>()));

        AskPass *askPass = AskPass::instance();
        emit askPass->promptRequested(901, u"Username for 'https://example.com': "_s, false);
        emit askPass->promptRequested(902, u"Password for 'https://alice@example.com': "_s, true);
        QQuickItem *answer = nullptr;
        QTRY_VERIFY((answer = findItem(window->contentItem(), u"credentialAnswer"_s)) && answer->isVisible());
        auto *dialog = window->findChild<QObject *>(u"credentialDialog"_s);
        QCOMPARE(dialog->property("promptId").toInt(), 901);
        QCOMPARE(answer->property("echoMode").toInt(), 0); // Normal
        answer->setProperty("text", u"alice"_s);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(dialog->property("promptId").toInt(), 902);
        QTRY_VERIFY(answer->isVisible());
        QCOMPARE(answer->property("echoMode").toInt(), 2); // Password
        QCOMPARE(answer->property("text").toString(), QString());
        // The helper going away closes it
        emit askPass->promptCanceled(902);
        QTRY_VERIFY(!dialog->property("visible").toBool());
    }

    void discardsAndStashes()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        const auto write = [&](const QString &name, const QByteArray &content) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(content);
        };
        const auto read = [&](const QString &name) {
            QFile file(dir.filePath(name));
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        write(u"a.txt"_s, "one\ntwo\nthree\n");
        write(u"b.txt"_s, "b\n");
        git.run({u"add"_s, u"."_s});
        git.run({u"commit"_s, u"-q"_s, u"-m"_s, u"One"_s});
        write(u"a.txt"_s, "ONE\ntwo\nTHREE\n");
        write(u"b.txt"_s, "b changed\n");

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QTRY_COMPARE(window->property("mode").toInt(), 1);

        // A file, from its menu
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = fileRow(window, u"unstagedList"_s, u"b.txt"_s)));
        QTest::mouseClick(window, Qt::RightButton, {}, row->mapToScene({row->width() / 2, row->height() / 2}).toPoint());
        click(window, u"discardFileMenuItem"_s);
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"discardOkButton"_s)->isVisible());
        QCOMPARE(window->findChild<QQuickItem *>(u"discardOkButton"_s)->property("text").toString(), u"Discard"_s);
        click(window, u"discardOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(read(u"b.txt"_s), "b\n"_ba);

        // Lines, from the diff's selection
        repo->showFile(u"a.txt"_s, false);
        QVERIFY(settle(*repo));
        for (int r = 0; r < repo->diff()->rowCount(); ++r) {
            const QString text = repo->diff()->data(repo->diff()->index(r), DiffModel::TextRole).toString();
            if (text == u"three"_s || text == u"THREE"_s)
                repo->diff()->toggleLine(r);
        }
        QTRY_COMPARE(repo->diff()->selectedCount(), 2);
        click(window, u"discardLinesButton"_s);
        click(window, u"discardOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(read(u"a.txt"_s), "ONE\ntwo\nthree\n"_ba);

        // Stashed from the tool bar
        click(window, u"stashButton"_s);
        click(window, u"stashChangesItem"_s);
        auto *message = window->findChild<QQuickItem *>(u"stashMessage"_s);
        QTRY_VERIFY(message && message->isVisible());
        message->setProperty("text", u"The first line"_s);
        click(window, u"stashOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(read(u"a.txt"_s), "one\ntwo\nthree\n"_ba);
        QCOMPARE(repo->stashes().size(), 1);
        QVERIFY(repo->stashes().first().contains(u"The first line"_s));
        QTRY_COMPARE(window->findChild<QQuickItem *>(u"stashButton"_s)->property("text").toString(), u"1"_s);
        // Listed in its menu, with what can be done to it
        click(window, u"stashButton"_s);
        QTRY_VERIFY(window->property("popupOpen").toBool());
        QTest::qWait(200);
        screenshot(window, u"stash-menu"_s);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("popupOpen").toBool());

        // Dropped after the guard
        QVERIFY(QMetaObject::invokeMethod(window, "askDropStash", Q_ARG(int, 0)));
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"dropStashOkButton"_s)->isVisible());
        click(window, u"dropStashOkButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(repo->stashes().isEmpty());
    }

    // A commit's menu resets the branch to it, the mode chosen in a dialog,
    // and reverts it.
    void resetsAndReverts()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        const auto commit = [&](const QString &name, const QByteArray &content) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(content);
            file.close();
            git.run({u"add"_s, name});
            git.run({u"commit"_s, u"-q"_s, u"-m"_s, name});
        };
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        commit(u"a.txt"_s, "a\n");
        commit(u"b.txt"_s, "b\n");
        commit(u"c.txt"_s, "c\n");

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));
        const QString second = repo->commits()->shaAt(1);

        rightClickCommit(window, 1);
        click(window, u"resetMenuItem"_s);
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"resetOkButton"_s)->isVisible());
        click(window, u"resetHard"_s);
        click(window, u"resetOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->headSha(), second);
        QVERIFY(!QFile::exists(dir.filePath(u"c.txt"_s)));

        rightClickCommit(window, 0);
        click(window, u"revertMenuItem"_s);
        QVERIFY(settle(*repo));
        QVERIFY(!QFile::exists(dir.filePath(u"b.txt"_s)));
        QCOMPARE(repo->commits()->rowCount(), 3);
    }

    // A tag made at a commit from its menu, compared, and deleted from its
    // label's menu after the guard.
    void tagsAndCompares()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        const auto commit = [&](const QString &name) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(name.toUtf8() + '\n');
            file.close();
            git.run({u"add"_s, name});
            git.run({u"commit"_s, u"-q"_s, u"-m"_s, name});
        };
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        commit(u"a.txt"_s);
        commit(u"b.txt"_s);
        commit(u"c.txt"_s);

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));

        rightClickCommit(window, 2);
        click(window, u"createTagMenuItem"_s);
        auto *name = window->findChild<QQuickItem *>(u"tagName"_s);
        QTRY_VERIFY(name && name->isVisible());
        name->setProperty("text", u"v0.1"_s);
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"tagOkButton"_s)->isEnabled());
        click(window, u"tagOkButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(repo->tags().contains(u"v0.1"_s));

        // The newest compared with the oldest, shown
        repo->showCommit(repo->commits()->shaAt(2));
        QVERIFY(settle(*repo));
        rightClickCommit(window, 0);
        click(window, u"compareMenuItem"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->diffSource(), Repository::CompareDiff);
        QCOMPARE(repo->diff()->fileCount(), 2);

        QQuickItem *label = nullptr;
        QTRY_VERIFY((label = [&]() -> QQuickItem * {
            for (QQuickItem *item : findItems(window->contentItem(), u"refLabel"_s)) {
                if (item->property("name").toString() == u"v0.1"_s)
                    return item;
            }
            return nullptr;
        }()));
        QVERIFY(QQuickTest::qWaitForPolish(window));
        QTest::mouseClick(window, Qt::RightButton, {}, label->mapToScene({label->width() / 2, label->height() / 2}).toPoint());
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"deleteTagMenuItem"_s)->isVisible());
        QVERIFY(!window->findChild<QQuickItem *>(u"checkoutMenuItem"_s)->isVisible());
        click(window, u"deleteTagMenuItem"_s);
        click(window, u"deleteTagOkButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(!repo->tags().contains(u"v0.1"_s));
    }

    // The repository menu's remotes, a bisection from a commit's menu with
    // its banner, and a clone from the menu's dialog.
    void managesTheRepository()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        const auto commit = [&](const QString &name) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(name.toUtf8() + '\n');
            file.close();
            git.run({u"add"_s, name});
            git.run({u"commit"_s, u"-q"_s, u"-m"_s, name});
        };
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        for (const QString &name : {u"a.txt"_s, u"b.txt"_s, u"c.txt"_s, u"d.txt"_s})
            commit(name);
        QTemporaryDir bare;
        GitRunner(bare.path()).run({u"clone"_s, u"-q"_s, u"--bare"_s, dir.path(), bare.filePath(u"copy.git"_s)});

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));

        // A remote added from the menu's dialog, and removed after asking
        click(window, u"repositoryButton"_s);
        click(window, u"remotesItem"_s);
        click(window, u"addRemoteButton"_s);
        auto *name = window->findChild<QQuickItem *>(u"remoteName"_s);
        QTRY_VERIFY(name && name->isVisible());
        QCOMPARE(name->property("text").toString(), u"origin"_s);
        window->findChild<QQuickItem *>(u"remoteUrl"_s)->setProperty("text", bare.filePath(u"copy.git"_s));
        click(window, u"remoteOkButton"_s);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->remotes().size(), 1);
        // Made by a Repeater, so in the scene, not among the window's children
        QQuickItem *remove = nullptr;
        QTRY_VERIFY((remove = findItem(window->contentItem(), u"removeRemoteButton"_s)));
        clickItem(window, remove);
        click(window, u"removeRemoteOkButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(repo->remotes().isEmpty());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("popupOpen").toBool());

        // A bisection: bad here, good there, then stopped from the banner
        rightClickCommit(window, 0);
        click(window, u"bisectBadMenuItem"_s);
        QVERIFY(settle(*repo));
        QVERIFY(repo->bisecting());
        rightClickCommit(window, 3);
        click(window, u"bisectGoodMenuItem"_s);
        QVERIFY(settle(*repo));
        auto *banner = window->findChild<QQuickItem *>(u"bisectBanner"_s);
        QTRY_VERIFY(banner->isVisible());
        click(window, u"bisectStopButton"_s);
        QVERIFY(settle(*repo));
        QVERIFY(!repo->bisecting());
        QTRY_VERIFY(!banner->isVisible());

        // A clone, opened once there
        QTemporaryDir parent;
        click(window, u"repositoryButton"_s);
        click(window, u"cloneRepositoryItem"_s);
        auto *url = window->findChild<QQuickItem *>(u"cloneUrl"_s);
        QTRY_VERIFY(url && url->isVisible());
        url->setProperty("text", bare.filePath(u"copy.git"_s));
        window->findChild<QQuickItem *>(u"cloneParent"_s)->setProperty("text", parent.path());
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"cloneOkButton"_s)->isEnabled());
        click(window, u"cloneOkButton"_s);
        QVERIFY(settle(*repo));
        QTRY_COMPARE(repo->path(), QDir(parent.filePath(u"copy"_s)).canonicalPath());
        QCOMPARE(repo->commits()->rowCount(), 4);
    }

    void blocksThePointerBehindModalDialogs()
    {
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QVERIFY(QMetaObject::invokeMethod(window, "showHistory"));
        auto *list = window->findChild<QQuickItem *>(u"historyList"_s);
        QVERIFY(list);
        list->setProperty("currentIndex", 0);

        // Every drag handler of the window, delegates' too: they are no
        // QObject children of the window, only of their items.
        const auto dragHandlers = [window] {
            QList<QObject *> handlers;
            QList<QQuickItem *> items{window->contentItem()};
            while (!items.isEmpty()) {
                QQuickItem *item = items.takeLast();
                for (QObject *child : item->children()) {
                    if (child->inherits("QQuickDragHandler"))
                        handlers.append(child);
                }
                items += item->childItems();
            }
            return handlers;
        };
        const auto anyActive = [&](const QString &name) {
            for (QObject *handler : dragHandlers()) {
                if (handler->objectName() == name && handler->property("active").toBool())
                    return true;
            }
            return false;
        };
        // Presses at `start` and drags down over the history.
        const auto drag = [&](QPoint start) {
            // As a mouse does: it comes to the place before it presses.
            for (int i = 5; i >= 0; --i)
                QTest::mouseMove(window, start + QPoint(8 * i, 0));
            QTest::mousePress(window, Qt::LeftButton, {}, start);
            bool dragged = false;
            for (int i = 1; i <= 10; ++i) {
                QTest::mouseMove(window, start + QPoint(0, 12 * i));
                dragged = dragged || anyActive(u"commitDrag"_s);
            }
            QTest::mouseRelease(window, Qt::LeftButton, {}, start + QPoint(0, 120));
            QTest::mouseClick(window, Qt::LeftButton, {}, start + QPoint(0, 60));
            return dragged;
        };

        // A commit was chosen with the mouse before: its handlers must not
        // keep the pointer.
        QQuickItem *chosen = historyRow(window, 0);
        QVERIFY(chosen);
        QTest::mouseClick(window, Qt::LeftButton, {}, chosen->mapToScene({600, chosen->height() / 2}).toPoint());

        QVERIFY(QMetaObject::invokeMethod(window, "openSettings"));
        auto *dialog = window->findChild<QObject *>(u"settingsDialog"_s);
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        // Where the dialog has nothing to press: its padding at the right
        // edge, and a spot of its content beside a button; both over the
        // history.
        auto *background = dialog->property("background").value<QQuickItem *>();
        auto *content = dialog->property("contentItem").value<QQuickItem *>();
        QVERIFY(background && content);
        for (const QPoint inside : {background->mapToScene({background->width() - 6, 60}).toPoint(),
                                    content->mapToScene({content->width() - 20, 150}).toPoint()}) {
            QVERIFY(list->contains(list->mapFromScene(inside)));
            QVERIFY2(!drag(inside), qPrintable(u"a commit was dragged from %1,%2 in the settings"_s
                                                   .arg(inside.x()).arg(inside.y())));
            QCOMPARE(list->property("currentIndex").toInt(), 0);
            QVERIFY(dialog->property("opened").toBool());
        }

        // Qt still offers a press that something in the dialog takes to the
        // handlers beneath it, so they are off while it is open.
        const auto handlersEnabled = [&] {
            QStringList on;
            for (QObject *handler : dragHandlers()) {
                if (handler->property("enabled").toBool())
                    on.append(handler->objectName().isEmpty() ? QString::fromLatin1(handler->metaObject()->className())
                                                              : handler->objectName());
            }
            return on;
        };
        QCOMPARE(handlersEnabled(), QStringList());

        // Beside the dialog: it closes, and the press is spent on that.
        QQuickItem *row = historyRow(window, 1);
        QVERIFY(row);
        QVERIFY2(!drag(row->mapToScene({20, row->height() / 2}).toPoint()),
                 "a commit was dragged beside the settings");
        QTRY_VERIFY(!dialog->property("opened").toBool());
        QVERIFY(!handlersEnabled().isEmpty());

        // A menu holds them off as well, and so does a combo box's list.
        rightClickCommit(window, 2);
        QTRY_VERIFY(window->property("popupOpen").toBool());
        QCOMPARE(handlersEnabled(), QStringList());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("popupOpen").toBool());
        QVERIFY(!handlersEnabled().isEmpty());

        click(window, u"branchScope"_s);
        QTRY_VERIFY(window->property("popupOpen").toBool());
        QCOMPARE(handlersEnabled(), QStringList());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!window->property("popupOpen").toBool());
        QVERIFY(!handlersEnabled().isEmpty());
    }

    // The compose window lists the working tree's hunks and puts them into
    // clusters, whichever concept of its view is built, without a warning.
    void composesClusters()
    {
        // Changes of its own: the tests before may have committed all
        const QString path = QDir(m_dir.path()).filePath(u"compose.txt"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("first\nsecond\n");
        }
        const auto remove = qScopeGuard([&] { QFile::remove(path); });

        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &w) { warnings += w; });
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));

        QVERIFY(QMetaObject::invokeMethod(window, "openCompose"));
        QQuickWindow *compose = nullptr;
        QTRY_VERIFY([&] {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                if (w != window && w->isVisible() && w->findChild<ChangeClusters *>())
                    compose = qobject_cast<QQuickWindow *>(w);
            }
            return compose != nullptr;
        }());
        QVERIFY(QTest::qWaitForWindowExposed(compose));
        auto *clusters = compose->findChild<ChangeClusters *>();
        QVERIFY(clusters->unassignedCount() > 0);
        QCOMPARE(clusters->unassignedCount(), clusters->hunks().size());

        const int cluster = clusters->addCluster(u"Feature"_s);
        clusters->setClusterMessage(cluster, u"Add the compose notes\n\nWhy they are needed."_s);
        QString composeHunk;
        for (const QVariant &hunk : clusters->hunks()) {
            if (hunk.toMap().value(u"path"_s) == "compose.txt"_L1)
                composeHunk = hunk.toMap().value(u"id"_s).toString();
        }
        QVERIFY(!composeHunk.isEmpty());
        clusters->assign(composeHunk, cluster);
        QTRY_COMPARE(clusters->unassignedCount(), clusters->hunks().size() - 1);

        QVERIFY(QQuickTest::qWaitForPolish(compose));
        screenshot(compose, u"compose-groups"_s);
        QCOMPARE(clusters->clusters().size(), 1);

        // While planning, the cluster's commit goes into the plan, and the
        // changes are read against it
        repo->startPlanning();
        QVERIFY(settle(*repo));
        QTRY_VERIFY(repo->planning());
        QVERIFY(QMetaObject::invokeMethod(composeView(compose), "commitCluster", Q_ARG(int, cluster)));
        QVERIFY(settle(*repo));
        QTRY_COMPARE(repo->plan()->count(), 1);
        QTRY_VERIFY(clusters->clusters().isEmpty());
        const auto stillChanged = [clusters] {
            const QVariantList hunks = clusters->hunks();
            return std::any_of(hunks.cbegin(), hunks.cend(), [](const QVariant &hunk) {
                return hunk.toMap().value(u"path"_s) == "compose.txt"_L1;
            });
        };
        QTRY_VERIFY(!stillChanged());
        QCOMPARE(gitOut({"log", "-1", "--format=%B", repo->headSha()}).trimmed(),
                 "Add the compose notes\n\nWhy they are needed."_ba);
        repo->discardPlan();
        QVERIFY(settle(*repo));
        QTRY_VERIFY(stillChanged());

        // A commit from outside the window: it reads the changes again
        git({"add", "compose.txt"});
        git({"commit", "-q", "-m", "Add the compose notes elsewhere"});
        repo->refresh();
        QVERIFY(settle(*repo));
        QTRY_VERIFY(!stillChanged());
        compose->close();

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.value(0).toString()));
    }

    // A card split open picks lines by dragging over them, and dragging
    // what is picked carries those lines onto a group, or onto the column
    // that makes a new one.
    void splitsACardInPlace()
    {
        const QString path = QDir(m_dir.path()).filePath(u"split.txt"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("one\ntwo\nthree\nfour\n");
        }
        const auto remove = qScopeGuard([&] { QFile::remove(path); });

        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &w) { warnings += w; });
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(settle(*window->findChild<Repository *>()));

        QVERIFY(QMetaObject::invokeMethod(window, "openCompose"));
        QQuickWindow *compose = nullptr;
        QTRY_VERIFY([&] {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                if (w != window && w->isVisible() && w->findChild<ChangeClusters *>())
                    compose = qobject_cast<QQuickWindow *>(w);
            }
            return compose != nullptr;
        }());
        QVERIFY(QTest::qWaitForWindowExposed(compose));
        auto *clusters = compose->findChild<ChangeClusters *>();
        QVERIFY(clusters);
        const auto close = qScopeGuard([compose] { compose->close(); });

        // The pieces of split.txt: {cluster: added}
        const auto pieces = [clusters] {
            QMap<int, int> added;
            for (const QVariant &piece : clusters->hunks()) {
                const QVariantMap map = piece.toMap();
                if (map.value(u"path"_s) == "split.txt"_L1)
                    added.insert(map.value(u"cluster"_s).toInt(), map.value(u"added"_s).toInt());
            }
            return added;
        };
        QCOMPARE(pieces(), (QMap<int, int>{{-1, 4}}));
        QString hunk;
        for (const QVariant &piece : clusters->hunks()) {
            if (piece.toMap().value(u"path"_s) == "split.txt"_L1)
                hunk = piece.toMap().value(u"hunk"_s).toString();
        }

        // The one card split open, and the point of one of its lines
        const auto openCard = [compose] {
            QQuickItem *card = nullptr;
            const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (!item->isVisible())
                    return;
                if (item->property("splitting").toBool() && item->property("carrying").isValid())
                    card = item;
                for (QQuickItem *child : item->childItems())
                    walk(child);
            };
            walk(compose->contentItem());
            return card;
        };
        const auto linePoint = [](QQuickItem *card, int row) {
            QQuickItem *lines = findItem(card, u"hunkLines"_s);
            const qreal height = card->property("lineHeight").toReal();
            return lines->mapToScene({40, height * row + height / 2}).toPoint();
        };
        const auto drag = [compose](QPoint from, QPoint to) {
            QTest::mousePress(compose, Qt::LeftButton, {}, from);
            constexpr int steps = 10;
            for (int i = 1; i <= steps; ++i)
                QTest::mouseMove(compose, from + (to - from) * i / steps);
            QTest::qWait(30);
            QTest::mouseRelease(compose, Qt::LeftButton, {}, to);
        };

        // Lines picked as in any list, and two carried into a new group.
        // Lines of a new file are rows 0 to 3, their index the same.
        QVERIFY(QMetaObject::invokeMethod(composeView(compose), "splitHunk", Q_ARG(QString, hunk + u"/none"_s)));
        QVERIFY(QQuickTest::qWaitForPolish(compose));
        QQuickItem *card = openCard();
        QVERIFY(card);
        const auto picked = [&card] {
            QList<int> rows;
            for (const QVariant &index : card->property("picked").toList())
                rows.append(index.toInt());
            std::sort(rows.begin(), rows.end());
            return rows;
        };
        const auto clickLine = [&](int row, Qt::KeyboardModifiers modifiers = {}) {
            QTest::mouseClick(compose, Qt::LeftButton, modifiers, linePoint(card, row));
        };
        drag(linePoint(card, 0), linePoint(card, 1));
        QTRY_COMPARE(picked(), (QList<int>{0, 1}));
        clickLine(3, Qt::ControlModifier);
        QTRY_COMPARE(picked(), (QList<int>{0, 1, 3}));
        clickLine(0, Qt::ControlModifier);
        QTRY_COMPARE(picked(), (QList<int>{1, 3}));
        clickLine(0, Qt::ShiftModifier);
        QTRY_COMPARE(picked(), (QList<int>{0}));
        clickLine(2, Qt::ShiftModifier);
        QTRY_COMPARE(picked(), (QList<int>{0, 1, 2}));
        clickLine(3);
        QTRY_COMPARE(picked(), (QList<int>{3}));
        clickLine(0, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(picked(), (QList<int>{0, 1, 2, 3}));
        clickLine(0);
        clickLine(1, Qt::ShiftModifier);
        QTRY_COMPARE(picked(), (QList<int>{0, 1}));
        screenshot(compose, u"split-picked"_s);
        auto *newGroup = findItem(compose->contentItem(), u"newGroupDrop"_s);
        QVERIFY(newGroup);
        drag(linePoint(card, 1), newGroup->mapToScene({newGroup->width() / 2, newGroup->height() / 2}).toPoint());
        QTRY_COMPARE(clusters->clusters().size(), 1);
        const int group = clusters->clusters().first().toMap().value(u"id"_s).toInt();
        QTRY_COMPARE(pieces(), (QMap<int, int>{{-1, 2}, {group, 2}}));
        // The rest stays open, to split further
        QCOMPARE(composeView(compose)->property("splitPiece").toString(), hunk + u"/none"_s);

        // One more, carried into that group
        QVERIFY(QQuickTest::qWaitForPolish(compose));
        card = openCard();
        QVERIFY(card);
        // Line indexes are the hunk's: what is left starts at three
        clickLine(0);
        QTRY_COMPARE(picked(), (QList<int>{2}));
        auto *column = findItem(compose->contentItem(), u"groupColumn"_s);
        QVERIFY(column);
        drag(linePoint(card, 0), column->mapToScene({column->width() / 2, column->height() - 80}).toPoint());
        QTRY_COMPARE(pieces(), (QMap<int, int>{{-1, 1}, {group, 3}}));
        screenshot(compose, u"split-groups"_s);

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.value(0).toString()));
    }

    // The commits not merged yet stand before the groups: a change dropped
    // onto one is folded into it, a change of one dragged onto the place
    // for a new group leaves it for the working tree, in that group, and
    // Undo takes that back. A commit a remote branch has refuses changes;
    // a pinned group stays in sight while the board scrolls, by the wheel
    // with Shift or the timeline.
    void foldsChangesIntoTheHistory()
    {
        const auto text = [](const QHash<int, QByteArray> &changes) {
            QByteArray content;
            for (int i = 1; i <= 20; ++i)
                content += changes.value(i, "line " + QByteArray::number(i)) + '\n';
            return content;
        };
        const QString path = QDir(m_dir.path()).filePath(u"hist.txt"_s);
        const auto write = [&path](const QByteArray &content) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(content);
        };
        write(text({}));
        git({"add", "hist.txt"});
        git({"commit", "-q", "-m", "Add hist"});
        write(text({{2, "two"}}));
        git({"commit", "-q", "-am", "Change two"});
        write(text({{2, "two"}, {15, "fifteen"}}));

        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &w) { warnings += w; });
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));

        QVERIFY(QMetaObject::invokeMethod(window, "openCompose"));
        QQuickWindow *compose = nullptr;
        QTRY_VERIFY([&] {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                if (w != window && w->isVisible() && w->findChild<ChangeClusters *>())
                    compose = qobject_cast<QQuickWindow *>(w);
            }
            return compose != nullptr;
        }());
        QVERIFY(QTest::qWaitForWindowExposed(compose));
        auto *clusters = compose->findChild<ChangeClusters *>();
        const auto close = qScopeGuard([compose] { compose->close(); });

        // The card of hist.txt under `root` changing `line`
        const auto cardOf = [](QQuickItem *root, const QString &line) {
            QQuickItem *card = nullptr;
            const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (!item->isVisible() || card)
                    return;
                const QVariantMap hunk = item->property("hunk").toMap();
                if (item->property("carrying").isValid() && hunk.value(u"path"_s) == "hist.txt"_L1) {
                    for (const QVariant &l : hunk.value(u"lines"_s).toList()) {
                        if (l.toMap().value(u"text"_s) == line)
                            card = item;
                    }
                }
                for (QQuickItem *child : item->childItems())
                    walk(child);
            };
            walk(root);
            return card;
        };
        const auto drag = [compose](QQuickItem *from, QQuickItem *onto, qreal y) {
            QVERIFY(from && onto);
            const QPoint start = from->mapToScene({from->width() / 2, 12}).toPoint();
            const QPoint end = onto->mapToScene({onto->width() / 2, y}).toPoint();
            QVERIFY2(QRect(QPoint(), compose->size()).contains(end), "the place to drop is out of sight");
            QTest::mousePress(compose, Qt::LeftButton, {}, start);
            for (int i = 1; i <= 10; ++i)
                QTest::mouseMove(compose, start + (end - start) * i / 10);
            QTest::qWait(30);
            QTest::mouseRelease(compose, Qt::LeftButton, {}, end);
        };
        const auto headColumn = [compose] {
            const QList<QQuickItem *> columns = findItems(compose->contentItem(), u"commitColumn"_s);
            return columns.isEmpty() ? nullptr : columns.last();
        };

        // The newest commit is in sight, next to the groups
        QVERIFY(QQuickTest::qWaitForPolish(compose));
        QQuickItem *head = headColumn();
        QVERIFY(head);
        QCOMPARE(head->property("modelData").toMap().value(u"subject"_s), "Change two"_L1);
        screenshot(compose, u"compose-history"_s);

        // Folded into HEAD
        drag(cardOf(compose->contentItem(), u"fifteen"_s), head, head->height() - 60);
        QVERIFY(settle(*repo));
        QTRY_COMPARE(gitOut({"show", "HEAD:hist.txt"}), text({{2, "two"}, {15, "fifteen"}}));
        QCOMPARE(gitOut({"log", "-1", "--format=%s"}).trimmed(), "Change two"_ba);
        QTRY_VERIFY(!cardOf(findItem(compose->contentItem(), u"groupsView"_s), u"fifteen"_s)
                    || cardOf(headColumn(), u"fifteen"_s));

        // Out of HEAD again, into a new group
        QTRY_VERIFY(cardOf(headColumn(), u"fifteen"_s));
        auto *newGroup = findItem(compose->contentItem(), u"newGroupDrop"_s);
        drag(cardOf(headColumn(), u"fifteen"_s), newGroup, newGroup->height() / 2);
        QVERIFY(settle(*repo));
        QTRY_COMPARE(gitOut({"show", "HEAD:hist.txt"}), text({{2, "two"}}));
        QTRY_COMPARE(clusters->clusters().size(), 1);
        const int group = clusters->clusters().first().toMap().value(u"id"_s).toInt();
        QTRY_VERIFY([&] {
            for (const QVariant &piece : clusters->hunks()) {
                const QVariantMap map = piece.toMap();
                if (map.value(u"path"_s) == "hist.txt"_L1)
                    return map.value(u"cluster"_s).toInt() == group;
            }
            return false;
        }());
        screenshot(compose, u"compose-taken-out"_s);

        // Undo puts it back into HEAD
        click(compose, u"composeUndoButton"_s);
        QVERIFY(settle(*repo));
        QTRY_COMPARE(gitOut({"show", "HEAD:hist.txt"}), text({{2, "two"}, {15, "fifteen"}}));
        QVERIFY(gitOut({"diff", "HEAD", "--", "hist.txt"}).isEmpty());

        // A commit a remote branch has stays as it is: a change held over
        // it is refused, and the column says why
        git({"update-ref", "refs/remotes/origin/feature", "HEAD~1"});
        write(text({{2, "two"}, {15, "fifteen"}, {18, "eighteen"}}));
        QVERIFY(QMetaObject::invokeMethod(clusters, "reload"));
        QQuickItem *pushed = nullptr;
        QTRY_VERIFY([&] {
            for (QQuickItem *column : findItems(compose->contentItem(), u"commitColumn"_s)) {
                const QVariantMap commit = column->property("modelData").toMap();
                if (commit.value(u"subject"_s) == "Add hist"_L1 && commit.value(u"published"_s).toBool())
                    pushed = column;
            }
            return pushed != nullptr;
        }());
        QVERIFY(findItem(pushed, u"pushedTag"_s));
        // Scrolled to it, as the board does: by the column shown first
        auto *groupsView = findItem(compose->contentItem(), u"groupsView"_s);
        const auto scrollTo = [&](int column) {
            groupsView->setProperty("firstColumn", column);
            QVERIFY(QMetaObject::invokeMethod(groupsView, "placeBoard"));
            QTest::qWait(30);
            QVERIFY(QQuickTest::qWaitForPolish(compose));
        };
        scrollTo(pushed->property("index").toInt());
        QVERIFY(QRectF(0, 0, compose->width(), compose->height())
                    .contains(pushed->mapToScene({pushed->width() / 2, pushed->height() / 2})));
        const QByteArray before = gitOut({"rev-parse", "HEAD~1"});
        {
            QQuickItem *card = cardOf(compose->contentItem(), u"eighteen"_s);
            QVERIFY(card);
            const QPoint start = card->mapToScene({card->width() / 2, 12}).toPoint();
            const QPoint end = pushed->mapToScene({pushed->width() / 2, pushed->height() / 2}).toPoint();
            QTest::mousePress(compose, Qt::LeftButton, {}, start);
            for (int i = 1; i <= 10; ++i)
                QTest::mouseMove(compose, start + (end - start) * i / 10);
            QTRY_VERIFY(findItem(pushed, u"refusedNote"_s));
            screenshot(compose, u"compose-refused"_s);
            QTest::mouseRelease(compose, Qt::LeftButton, {}, end);
        }
        QVERIFY(settle(*repo));
        QCOMPARE(gitOut({"rev-parse", "HEAD~1"}), before);
        QVERIFY(gitOut({"diff", "HEAD", "--", "hist.txt"}).contains("+eighteen"));
        QVERIFY(!findItem(pushed, u"refusedNote"_s));

        // A pinned group stays in sight, the board scrolled back to the
        // oldest commit
        scrollTo(clusters->commits().size());
        QQuickItem *groupColumn = findItem(compose->contentItem(), u"groupColumn"_s);
        QVERIFY(groupColumn);
        clickItem(compose, findItem(groupColumn, u"pinButton"_s));
        QTRY_VERIFY(clusters->clusters().first().toMap().value(u"pinned"_s).toBool());
        scrollTo(0);
        groupColumn = findItem(compose->contentItem(), u"groupColumn"_s);
        QVERIFY(groupColumn);
        const QRectF shown = groupColumn->mapRectToScene({0, 0, groupColumn->width(), groupColumn->height()});
        QVERIFY(QRectF(0, 0, compose->width(), compose->height()).contains(shown));
        screenshot(compose, u"compose-pinned"_s);

        // Shift and the wheel scroll sideways; the timeline above scrolls
        // to where it is clicked, its newest end to HEAD
        {
            const QPointF at = groupsView->mapToScene({groupsView->width() / 2, groupsView->height() / 2});
            QWheelEvent wheel(at, compose->mapToGlobal(at), {}, {0, -120}, Qt::NoButton, Qt::ShiftModifier,
                              Qt::NoScrollPhase, false);
            QGuiApplication::sendEvent(compose, &wheel);
        }
        QTRY_VERIFY(groupsView->property("firstColumn").toReal() > 0);
        auto *timeline = findItem(compose->contentItem(), u"composeTimeline"_s);
        QVERIFY(timeline);
        QTest::mouseClick(compose, Qt::LeftButton, {},
                          timeline->mapToScene({timeline->width() - 1, timeline->height() / 2}).toPoint());
        // As far as the board goes: HEAD in sight, if not the first column
        QTRY_VERIFY(groupsView->property("firstColumn").toReal() > clusters->commits().size() - 2);
        QTest::qWait(30);
        QVERIFY(QQuickTest::qWaitForPolish(compose));
        QQuickItem *newest = findItems(compose->contentItem(), u"commitColumn"_s).last();
        QVERIFY(QRectF(0, 0, compose->width(), compose->height())
                    .contains(newest->mapRectToScene({0, 0, newest->width(), newest->height()})));
        screenshot(compose, u"compose-timeline"_s);

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.value(0).toString()));
    }

    // A long commit message: when the details grow with the window, the
    // lines they gained show, not only after scrolling.
    void showsLongCommitDetailsAfterGrowing()
    {
        QTemporaryDir dir;
        GitRunner git(dir.path());
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        git.run({u"config"_s, u"user.name"_s, u"Ada"_s});
        git.run({u"config"_s, u"user.email"_s, u"ada@example.invalid"_s});
        QFile file(dir.filePath(u"a.txt"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("a\n");
        file.close();
        git.run({u"add"_s, u"a.txt"_s});
        QString message = u"Add a long story\n\n"_s;
        for (int i = 0; i < 40; ++i)
            message += u"Line %1 of a long commit message that goes on and on.\n"_s.arg(i);
        git.run({u"commit"_s, u"-q"_s, u"-m"_s, message});

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(1000, 600);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        repo->showCommit(repo->commits()->shaAt(0));
        QVERIFY(settle(*repo));
        auto *details = window->findChild<QQuickItem *>(u"commitDetailsView"_s);
        QVERIFY(details);
        QTRY_VERIFY(details->isVisible() && details->height() > 50);
        const qreal before = details->height();
        QTest::qWait(200);

        window->resize(1000, 1200);
        QTRY_VERIFY(details->height() > before * 1.5);
        QTest::qWait(200);
        // The lower half of the details, which they gained, has text in it
        const QImage image = window->grabWindow();
        const QRectF rect = details->mapRectToScene(QRectF(16, details->height() / 2, details->width() - 32,
                                                           details->height() / 2 - 8));
        const qreal dpr = image.devicePixelRatio();
        const QColor background = image.pixelColor(QPointF(rect.left() * dpr, rect.top() * dpr).toPoint());
        int ink = 0;
        for (int y = int(rect.top() * dpr); y < int(rect.bottom() * dpr); y += 2) {
            for (int x = int(rect.left() * dpr); x < int(rect.right() * dpr); x += 2) {
                const QColor c = image.pixelColor(x, y);
                if (std::abs(c.lightness() - background.lightness()) > 60)
                    ++ink;
            }
        }
        QVERIFY2(ink > 200, qPrintable(u"only %1 pixels of text"_s.arg(ink)));
    }

    void benchComposeWindow()
    {
        if (qEnvironmentVariableIsEmpty("IGITT_BENCH"))
            QSKIP("set IGITT_BENCH to run");
        QTemporaryDir dir;
        GitRunner git;
        git.setWorkDir(dir.path());
        git.run({u"init"_s, u"-q"_s, u"-b"_s, u"main"_s});
        const auto writeFiles = [&](int round) {
            for (int f = 0; f < 5; ++f) {
                QFile file(QDir(dir.path()).filePath(u"file%1.txt"_s.arg(f)));
                QVERIFY(file.open(QIODevice::WriteOnly));
                for (int l = 0; l < 300; ++l)
                    file.write((l % 20 == round % 20 && l % 100 < 60 ? "changed " + QByteArray::number(round) + " "
                                                                       : QByteArray()) + "line " + QByteArray::number(l) + '\n');
            }
        };
        for (int round = 0; round < 31; ++round) {
            writeFiles(round);
            git.run({u"add"_s, u"-A"_s});
            git.run({u"commit"_s, u"-q"_s, u"-m"_s, u"Round %1"_s.arg(round)});
        }
        // Working tree: every tenth line of each file
        for (int f = 0; f < 5; ++f) {
            QFile file(QDir(dir.path()).filePath(u"file%1.txt"_s.arg(f)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            for (int l = 0; l < 300; ++l)
                file.write((l % 10 == 0 ? QByteArray("work ") : QByteArray()) + "line " + QByteArray::number(l) + '\n');
        }

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(settle(*window->findChild<Repository *>()));
        QElapsedTimer timer;
        timer.start();
        QVERIFY(QMetaObject::invokeMethod(window, "openCompose"));
        QQuickWindow *compose = nullptr;
        QTRY_VERIFY([&] {
            for (QWindow *w : QGuiApplication::topLevelWindows()) {
                if (w != window && w->isVisible() && w->findChild<ChangeClusters *>())
                    compose = qobject_cast<QQuickWindow *>(w);
            }
            return compose != nullptr;
        }());
        QVERIFY(QTest::qWaitForWindowExposed(compose));
        QVERIFY(QQuickTest::qWaitForPolish(compose));
        compose->grabWindow();
        const qint64 open = timer.restart();
        auto *clusters = compose->findChild<ChangeClusters *>();
        const int group = clusters->addCluster(u"Bench"_s);
        const QVariantList pieces = clusters->hunks();
        qint64 inAssign = 0;
        for (int i = 0; i < 20; ++i) {
            QElapsedTimer one;
            one.start();
            clusters->assign(pieces[i].toMap().value(u"id"_s).toString(), group);
            inAssign += one.elapsed();
            QVERIFY(QQuickTest::qWaitForPolish(compose));
            compose->grabWindow();
        }
        qWarning() << "BENCH in assign (model and bindings) ms" << inAssign;
        const qint64 assigns = timer.restart();
        QMetaObject::invokeMethod(clusters, "reload");
        QVERIFY(QQuickTest::qWaitForPolish(compose));
        compose->grabWindow();
        const qint64 reload = timer.restart();
        // A move: a working change folded into HEAD, until it shows
        const QVariantMap loose = clusters->hunks().last().toMap();
        const QString head = clusters->commits().last().toMap().value(u"sha"_s).toString();
        timer.restart();
        QVERIFY(QMetaObject::invokeMethod(composeView(compose), "placeInCommit", Q_ARG(QVariant, QVariant(loose)),
                                          Q_ARG(QString, head)));
        QTRY_VERIFY(!composeView(compose)->property("moving").toBool());
        QTRY_VERIFY(clusters->commits().last().toMap().value(u"sha"_s).toString() != head);
        QVERIFY(QQuickTest::qWaitForPolish(compose));
        compose->grabWindow();
        const qint64 move = timer.restart();
        qWarning() << "BENCH move ms" << move;
        qWarning() << "BENCH hunks" << pieces.size() << "commits" << clusters->commits().size() << "open ms" << open
                   << "20 assigns ms" << assigns << "reload ms" << reload;
        compose->close();
    }

    // Every theme reaches the window, the graph and the controls without a
    // warning, and a theme written into the user's folder is shown as soon
    // as it is saved.
    // A diff on display keeps its text when the theme changes: the lines
    // laid out before drew nothing of it after.
    void keepsDiffTextAcrossThemes()
    {
        Themes *themes = Themes::instance();
        const QString chosen = themes->theme();
        const auto restore = qScopeGuard([&] { themes->setTheme(chosen); });
        themes->setTheme(u"light"_s);

        write(u"themed.txt"_s, "one\ntwo\nthree\n");
        const auto cleanup = qScopeGuard([&] { QFile::remove(m_dir.filePath(u"themed.txt"_s)); });
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(settle(*repo));
        QVERIFY(QMetaObject::invokeMethod(window, "showChanges"));
        repo->showFile(u"themed.txt"_s, false);
        QVERIFY(settle(*repo));
        QQuickItem *line = diffLine(window, u"+two"_s);
        QVERIFY(line);
        // How much of the line differs from its background
        const auto ink = [&] {
            const QImage image = window->grabWindow();
            const qreal dpr = image.devicePixelRatio();
            const QRectF rect = line->mapRectToScene(QRectF(0, 0, line->width(), line->height()));
            const QColor background = image.pixelColor(QPointF((rect.right() + 4) * dpr, rect.center().y() * dpr).toPoint());
            int count = 0;
            for (int y = int(rect.top() * dpr); y < int(rect.bottom() * dpr); ++y) {
                for (int x = int(rect.left() * dpr); x < int(rect.right() * dpr); ++x) {
                    if (std::abs(image.pixelColor(x, y).lightness() - background.lightness()) > 60)
                        ++count;
                }
            }
            return count;
        };
        QTest::qWait(100);
        QVERIFY(ink() > 20);
        themes->setTheme(u"dark"_s);
        QTest::qWait(300);
        QVERIFY2(ink() > 20, "the line's text is gone after the theme changed");
    }

    void followsTheTheme()
    {
        Themes *themes = Themes::instance();
        const QString folder = themes->folder();
        const QString chosen = themes->theme();
        QTemporaryDir user;
        themes->setFolders(u":/qt/qml/IgittStyle/themes"_s, user.path());
        const auto restore = qScopeGuard([&] {
            themes->setTheme(chosen);
            themes->setFolders(u":/qt/qml/IgittStyle/themes"_s, folder);
        });

        QQmlApplicationEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this,
                [&warnings](const QList<QQmlError> &w) { warnings += w; });
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(repo);
        QVERIFY(settle(*repo));

        // Semantics.qml has a property for every role the theme files have.
        auto *tokens = engine.singletonInstance<QObject *>("IgittStyle", "TokenInterface");
        QVERIFY(tokens);
        auto *semantics = tokens->property("semantics").value<QObject *>();
        QVERIFY(semantics);
        QStringList properties;
        for (int i = semantics->metaObject()->propertyOffset(); i < semantics->metaObject()->propertyCount(); ++i)
            properties.append(QString::fromLatin1(semantics->metaObject()->property(i).name()));
        QStringList roles;
        for (const ThemeLibrary::Role &role : ThemeLibrary::roles())
            roles.append(role.name);
        properties.sort();
        roles.sort();
        QCOMPARE(properties, roles);

        const auto show = [themes](const QString &id) { themes->setTheme(id); };
        const QVariantList available = themes->available();
        QCOMPARE(available.size(), 8);
        for (const QVariant &theme : available) {
            const QString id = theme.toMap().value(u"id"_s).toString();
            show(id);
            QCOMPARE(themes->current(), id);
            QCOMPARE(themes->dark(), theme.toMap().value(u"dark"_s).toBool());
            const QVariantMap roles = themes->roles();
            QTRY_COMPARE(window->color(), roles.value(u"background_default"_s).value<QColor>());
            QCOMPARE(QColor::fromString(LaneColors::instance()->colorName(0)),
                     roles.value(u"graph_lanes"_s).toList().first().value<QColor>());
            screenshot(window, u"theme-"_s + id);
        }

        QFile file(QDir(user.path()).filePath(u"mine.json"_s));
        const auto save = [&file](const QByteArray &background) {
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(R"({"name": "Mine", "dark": true, "roles": {"background_default": ")" + background
                       + R"("}})");
            file.close();
        };
        save("#123456");
        QTRY_COMPARE(themes->available().size(), 9);
        show(u"mine"_s);
        QTRY_COMPARE(window->color(), QColor(u"#123456"_s));
        save("#654321");
        QTRY_COMPARE(window->color(), QColor(u"#654321"_s));
        QVERIFY(themes->errors().isEmpty());

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.value(0).toString()));
    }

    // A message written, or an amend begun, in one repository must not be
    // committed into the next one opened.
    void openingRepositoryResetsCommitForm()
    {
        QTemporaryDir other;
        GitRunner otherGit(other.path());
        QVERIFY(otherGit.run({"init", "-q", "-b", "main"}).ok());
        QVERIFY(otherGit.run({"commit", "-q", "--allow-empty", "-m", "Start elsewhere"}).ok());

        QQmlApplicationEngine engine;
        engine.setInitialProperties({{u"initialPath"_s, m_dir.path()}});
        engine.loadFromModule("Igitt", "Main");
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *repo = window->findChild<Repository *>();
        QVERIFY(repo);
        QVERIFY(settle(*repo));

        auto *title = window->findChild<QQuickItem *>(u"commitTitle"_s);
        auto *amend = window->findChild<QQuickItem *>(u"amendBox"_s);
        QVERIFY(title && amend);
        click(window, u"amendBox"_s);
        QVERIFY(amend->property("checked").toBool());
        QTRY_VERIFY(!title->property("text").toString().isEmpty());

        repo->open(other.path());
        QVERIFY(settle(*repo));
        QCOMPARE(repo->path(), QDir(other.path()).canonicalPath());
        QCOMPARE(title->property("text").toString(), QString());
        QVERIFY(!amend->property("checked").toBool());
        QCOMPARE(GitRunner(other.path()).run({"log", "--format=%s"}).out, "Start elsewhere\n"_ba);
    }

private:
    // Drags the fold handle of the diff view onto a row of the history.
    void dragSelectionOntoCommit(QQuickWindow *window, const QString &listName, int row)
    {
        auto *handle = window->findChild<QQuickItem *>(u"foldHandle"_s);
        auto *list = window->findChild<QQuickItem *>(listName);
        QVERIFY(handle && list);
        QTRY_VERIFY(handle->isVisible());
        QQuickItem *rowItem = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, rowItem),
                                          Q_ARG(int, row)));
        QVERIFY(rowItem);

        const QPoint from = handle->mapToScene({handle->width() / 2, handle->height() / 2}).toPoint();
        const QPoint to = rowItem->mapToScene({rowItem->width() / 3, rowItem->height() / 2}).toPoint();
        dragFromTo(window, from, to);
    }

    void dragFromTo(QQuickWindow *window, QPoint from, QPoint to)
    {
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        constexpr int steps = 12;
        for (int i = 1; i <= steps; ++i)
            QTest::mouseMove(window, from + (to - from) * i / steps);
        QTest::qWait(50);
        screenshot(window, u"dragging"_s);
        QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    }

    // The delete dialog is guarded: only the branch's name typed again
    // lets it delete
    void confirmDelete(QQuickWindow *window, const QString &branch)
    {
        auto *confirm = window->findChild<QQuickItem *>(u"deleteBranchConfirm"_s);
        auto *button = window->findChild<QQuickItem *>(u"deleteBranchOkButton"_s);
        QVERIFY(confirm && button);
        QTRY_VERIFY(confirm->isVisible());
        QVERIFY(!button->isEnabled());
        confirm->setProperty("text", branch + u"x"_s);
        QVERIFY(!button->isEnabled());
        confirm->setProperty("text", branch);
        QTRY_VERIFY(button->isEnabled());
        QCOMPARE(button->property("text").toString(), u"Delete Branch"_s);
        screenshot(window, u"delete-branch"_s);
        click(window, u"deleteBranchOkButton"_s);
    }

    QQuickItem *historyRow(QQuickWindow *window, int row)
    {
        if (!QQuickTest::qWaitForPolish(window))
            return nullptr;
        auto *list = window->findChild<QQuickItem *>(u"historyList"_s);
        QQuickItem *rowItem = nullptr;
        if (list)
            QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, rowItem), Q_ARG(int, row));
        return rowItem;
    }

    void rightClickCommit(QQuickWindow *window, int row)
    {
        QQuickItem *rowItem = historyRow(window, row);
        QVERIFY(rowItem);
        QTest::mouseClick(window, Qt::RightButton, {},
                          rowItem->mapToScene({rowItem->width() / 2, rowItem->height() / 2}).toPoint());
        QTest::qWait(50);
    }

    // Drags a history row onto another one.
    void dragCommit(QQuickWindow *window, int fromRow, int toRow)
    {
        QQuickItem *fromItem = historyRow(window, fromRow);
        QQuickItem *toItem = historyRow(window, toRow);
        QVERIFY(fromItem && toItem);
        const QPoint from = fromItem->mapToScene({fromItem->width() / 2, fromItem->height() / 2}).toPoint();
        const QPoint to = toItem->mapToScene({toItem->width() / 2, toItem->height() / 2}).toPoint();
        QTest::mousePress(window, Qt::LeftButton, {}, from);
        constexpr int steps = 10;
        for (int i = 1; i <= steps; ++i)
            QTest::mouseMove(window, from + (to - from) * i / steps);
        QTest::qWait(30);
        screenshot(window, u"commit-drag"_s);
        QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    }

    void click(QQuickWindow *window, const QString &objectName)
    {
        QVERIFY(QQuickTest::qWaitForPolish(window));
        auto *item = window->findChild<QQuickItem *>(objectName);
        QVERIFY2(item, qPrintable(objectName));
        QTRY_VERIFY(item->isVisible() && item->isEnabled());
        QTest::mouseClick(window, Qt::LeftButton, {},
                          item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    }

    // The view of a compose window, which composes the commits
    static QObject *composeView(QQuickWindow *window)
    {
        return window->findChild<QObject *>(u"composeView"_s);
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

    static QList<QQuickItem *> findItems(QQuickItem *root, const QString &objectName)
    {
        QList<QQuickItem *> found;
        if (!root->isVisible())
            return found;
        if (root->objectName() == objectName)
            found.append(root);
        for (QQuickItem *child : root->childItems())
            found += findItems(child, objectName);
        return found;
    }

    QQuickItem *fileRow(QQuickWindow *window, const QString &list, const QString &path)
    {
        // Positions are only right once a layout change has been polished.
        if (!QQuickTest::qWaitForPolish(window))
            return nullptr;
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
        QVERIFY(QQuickTest::qWaitForPolish(window));
        QVERIFY(item && item->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, {},
                          item->mapToScene({item->width() / 2, item->height() / 2}).toPoint());
    }

    // The text of the diff row showing `text`, marker included.
    static QQuickItem *diffLine(QQuickWindow *window, const QString &text)
    {
        if (!QQuickTest::qWaitForPolish(window))
            return nullptr;
        auto *list = window->findChild<QQuickItem *>(u"diffList"_s);
        if (!list)
            return nullptr;
        for (int row = 0; row < list->property("count").toInt(); ++row) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, row));
            QQuickItem *line = item ? findItem(item, u"lineText"_s) : nullptr;
            if (line && line->property("text").toString() == text)
                return line;
        }
        return nullptr;
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
