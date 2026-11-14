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

    void readsAFilesStatusAgainstHead()
    {
        const auto entry = [](QChar index, QChar worktree) {
            StatusEntry e;
            e.index = index;
            e.worktree = worktree;
            return e;
        };
        QCOMPARE(fileStatus(entry('.', 'M')), u"M"_s);
        QCOMPARE(fileStatus(entry('M', 'M')), u"M"_s);
        QCOMPARE(fileStatus(entry('A', 'M')), u"A"_s);
        QCOMPARE(fileStatus(entry('A', 'D')), QString()); // never was in HEAD
        QCOMPARE(fileStatus(entry('M', 'D')), u"D"_s);
        QCOMPARE(fileStatus(entry('D', '.')), u"D"_s);
        QCOMPARE(fileStatus(entry('R', 'M')), u"R"_s);
        StatusEntry untracked;
        untracked.untracked = true;
        QCOMPARE(fileStatus(untracked), u"A"_s);
        StatusEntry conflicted = entry('U', 'U');
        conflicted.conflicted = true;
        QCOMPARE(fileStatus(conflicted), u"U"_s);

        QCOMPARE(directoryStatus({}), QString());
        QCOMPARE(directoryStatus({u"A"_s}), u"A"_s);
        QCOMPARE(directoryStatus({u"D"_s}), u"D"_s);
        QCOMPARE(directoryStatus({u"A"_s, u"D"_s}), u"M"_s);
        QCOMPARE(directoryStatus({u"R"_s}), u"M"_s);
        QCOMPARE(directoryStatus({u"A"_s, u"U"_s}), u"U"_s);
    }

    // Changes mark the files and sum up in their directories; files not
    // tracked come and go as rows, without a reset that would collapse
    // the view.
    void marksTheTreeWithItsChanges()
    {
        const QStringList paths{u"src/main.cpp"_s, u"src/core/a.cpp"_s, u"src/core/b.cpp"_s, u"README.md"_s};
        FileTreeModel model;
        QAbstractItemModelTester tester(&model);
        model.setFiles(paths, buildFileTree(paths));
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        const auto change = [](const QString &path, QChar index, QChar worktree, bool untracked = false) {
            StatusEntry e;
            e.path = path;
            e.index = index;
            e.worktree = worktree;
            e.untracked = untracked;
            return e;
        };
        const auto status = [&model](const QModelIndex &index) { return index.data(FileTreeModel::StatusRole).toString(); };

        model.setStatus({change(u"src/core/a.cpp"_s, '.', 'D'), change(u"src/core/b.cpp"_s, 'D', '.'),
                         change(u"src/main.cpp"_s, '.', 'M'), change(u"docs/new.md"_s, '.', '.', true)});
        QCOMPARE(reset.size(), 0);
        // docs, src, README.md
        QCOMPARE(model.rowCount(), 3);
        const QModelIndex docs = model.index(0, 0);
        const QModelIndex src = model.index(1, 0);
        QCOMPARE(docs.data().toString(), u"docs"_s);
        QCOMPARE(status(docs), u"A"_s);
        QCOMPARE(status(model.index(0, 0, docs)), u"A"_s);
        const QModelIndex core = model.index(0, 0, src);
        QCOMPARE(status(core), u"D"_s); // both of its files deleted
        QCOMPARE(status(src), u"M"_s);
        QCOMPARE(status(model.index(1, 0, src)), u"M"_s);
        QCOMPARE(status(model.index(2, 0)), QString()); // README.md
        QCOMPARE(model.statuses().value(u"docs/new.md"_s).toString(), u"A"_s);
        QCOMPARE(model.match(u"new"_s), QStringList{u"docs/new.md"_s});

        // Committed or dropped, a file not tracked leaves, its directory
        // with it
        QSignalSpy statusChanged(&model, &FileTreeModel::statusChanged);
        model.setStatus({change(u"src/main.cpp"_s, '.', 'M')});
        QCOMPARE(reset.size(), 0);
        QCOMPARE(statusChanged.size(), 1);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.index(0, 0).data().toString(), u"src"_s);
        QCOMPARE(status(model.index(0, 0)), u"M"_s);
        QCOMPARE(status(model.index(0, 0, model.index(0, 0))), QString()); // core

        // New files that HEAD's tree does not know of yet stay through a
        // reload of the tracked files
        model.setStatus({change(u"src/new.h"_s, 'A', '.')});
        QCOMPARE(model.rowCount(model.index(0, 0)), 3);
        model.setFiles(paths + QStringList{u"x.txt"_s}, buildFileTree(paths + QStringList{u"x.txt"_s}));
        QCOMPARE(model.rowCount(model.index(0, 0)), 3);
        QCOMPARE(model.match(u"new.h"_s), QStringList{u"src/new.h"_s});
    }
};

QTEST_MAIN(TestModels)
#include "tst_models.moc"
