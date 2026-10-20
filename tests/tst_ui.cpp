// Loads the real UI against a small repository and fails on any QML warning.
// Set IGITT_SCREENSHOT_DIR to also save screenshots of the window.

#include "Repository.h"
#include "models/ConflictResolver.h"
#include "ui/Fonts.h"
#include "models/Timelapse.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QGuiApplication>
#include <QQuickItem>
#include <QtQuickTest/quicktest.h>
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
        if (qEnvironmentVariableIsEmpty("IGITT_TEST_DEFAULT_RHI"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QQuickStyle::setStyle(u"Fusion"_s);
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

        // History edits: reword from the context menu, move by drag and
        // drop, drop with confirmation; each can be undone.
        {
            const QByteArray before = gitOut({"log", "--format=%s"});
            rightClickCommit(window, 1);
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

            dragCommit(window, 0, 1);
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"log", "-2", "--format=%s"}), "Document line 15\nPolish line 28\n"_ba);
            repo->undoFold();
            QVERIFY(settle(*repo));
            QCOMPARE(gitOut({"log", "--format=%s"}), before);

            rightClickCommit(window, 0);
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
        click(window, u"filesTab"_s);
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
        // A click shows the file.
        clickItem(window, match);
        QVERIFY(settle(*repo));
        QCOMPARE(repo->diffSource(), Repository::FileContent);
        QCOMPARE(repo->diffPath(), u"feature.txt"_s);
        QCOMPARE(repo->diff()->data(repo->diff()->index(0), DiffModel::TextRole).toString(), u"feature"_s);
        QCOMPARE(repo->diff()->rowCount(), 1); // just the line, nothing to collapse
        QVERIFY(window->findChild<QQuickItem *>(u"fileTimelapseButton"_s)->isVisible());
        screenshot(window, u"file-content"_s);
        QTest::mouseDClick(window, Qt::LeftButton, {}, match->mapToScene({match->width() / 2, match->height() / 2}).toPoint());
        QQuickWindow *featureLapse = nullptr;
        QTRY_VERIFY((featureLapse = timelapseWindow()));
        QVERIFY(featureLapse->title().contains("feature.txt"_L1));
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
        // for gh. git pushes to the local server instead.
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
