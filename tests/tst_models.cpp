#include "core/DiffParser.h"
#include "models/DiffModel.h"
#include "models/FileTreeModel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

class TestModels : public QObject
{
    Q_OBJECT

private slots:
    void collapsesAndExpandsFiles()
    {
        DiffModel model;
        QAbstractItemModelTester tester(&model);
        model.setFiles(parseDiff("diff --git a/a.txt b/a.txt\n--- a/a.txt\n+++ b/a.txt\n@@ -1,2 +1,2 @@\n x\n-y\n+Y\n"
                                 "diff --git a/b.txt b/b.txt\n--- a/b.txt\n+++ b/b.txt\n@@ -1 +1 @@\n-p\n+P\n"_ba));
        QCOMPARE(model.fileCount(), 2);
        QCOMPARE(model.rowCount(), 9); // 2 headers, 2 hunk headers, 5 lines
        QCOMPARE(model.data(model.index(0), DiffModel::AddedRole).toInt(), 1);
        QCOMPARE(model.data(model.index(0), DiffModel::RemovedRole).toInt(), 1);

        model.toggleLine(4); // +Y
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        model.toggleFile(2); // any row of a.txt
        QCOMPARE(removed.size(), 1); // just its rows, no reset
        QCOMPARE(model.rowCount(), 5);
        QVERIFY(model.data(model.index(0), DiffModel::CollapsedRole).toBool());
        QCOMPARE(model.collapsedCount(), 1);
        QCOMPARE(model.data(model.index(1), DiffModel::PathRole).toString(), u"b.txt"_s);
        QCOMPARE(model.selectedCount(), 1); // kept while hidden

        model.toggleFile(0);
        QCOMPARE(model.rowCount(), 9);
        QVERIFY(model.data(model.index(4), DiffModel::SelectedRole).toBool());

        model.setAllCollapsed(true);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.collapsedCount(), 2);
        model.setAllCollapsed(false);
        QCOMPARE(model.rowCount(), 9);
    }

    void buildsAFileTree()
    {
        const QStringList paths{u"src/main.cpp"_s, u"README.md"_s, u"src/core/a.cpp"_s, u"src/b.h"_s, u"CMakeLists.txt"_s};
        FileTreeModel model;
        QAbstractItemModelTester tester(&model);
        model.setFiles(paths, buildFileTree(paths));
        QCOMPARE(model.fileCount(), 5);

        // Directories first, then files, case-insensitively.
        QCOMPARE(model.rowCount(), 3);
        const QModelIndex src = model.index(0, 0);
        QCOMPARE(src.data().toString(), u"src"_s);
        QVERIFY(src.data(FileTreeModel::IsDirRole).toBool());
        QCOMPARE(model.index(1, 0).data().toString(), u"CMakeLists.txt"_s);
        QCOMPARE(model.rowCount(src), 3);
        QCOMPARE(model.index(0, 0, src).data().toString(), u"core"_s);
        QCOMPARE(model.index(2, 0, src).data(FileTreeModel::PathRole).toString(), u"src/main.cpp"_s);
        QCOMPARE(model.parent(model.index(0, 0, src)), src);

        QCOMPARE(model.match(u"SRC cpp"_s), (QStringList{u"src/main.cpp"_s, u"src/core/a.cpp"_s}));
        QCOMPARE(model.match(u"cpp"_s, 1).size(), 1);

        // The same files again leave the view alone.
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        model.setFiles(paths, buildFileTree(paths));
        QCOMPARE(reset.size(), 0);
    }
};

QTEST_MAIN(TestModels)
#include "tst_models.moc"
