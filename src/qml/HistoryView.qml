pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Igitt.Core

Pane {
    id: view

    required property Repository repo

    property int dropRow: -1
    property string dropBlocker

    signal foldRequested(string sha)

    function rowAt(scenePos: point): int {
        const local = list.mapFromItem(null, scenePos.x, scenePos.y)
        if (local.x < 0 || local.y < 0 || local.x >= list.width || local.y >= list.height)
            return -1
        const content = list.contentItem.mapFromItem(null, scenePos.x, scenePos.y)
        return list.indexAt(content.x, content.y)
    }

    function updateDrop(scenePos: point) {
        const row = rowAt(scenePos)
        if (row === dropRow)
            return
        dropBlocker = row >= 0 ? repo.foldBlocker(repo.commits.shaAt(row)) : ""
        dropRow = row
    }

    function finishDrop(scenePos: point) {
        updateDrop(scenePos)
        const sha = dropRow >= 0 && dropBlocker === "" ? repo.commits.shaAt(dropRow) : ""
        dropRow = -1
        dropBlocker = ""
        if (sha !== "")
            foldRequested(sha)
    }

    padding: 0

    Menu {
        id: commitMenu

        property string sha
        property string blocker

        MenuItem {
            text: commitMenu.blocker === ""
                  ? qsTr("Fold %n selected line(s) into this commit", "", view.repo.diff.selectedCount)
                  : qsTr("Can't fold: %1").arg(commitMenu.blocker)
            enabled: commitMenu.blocker === ""
            onTriggered: view.foldRequested(commitMenu.sha)
        }
    }

    ListView {
        id: list

        objectName: "historyList"

        function showCurrent() {
            if (currentIndex >= 0)
                view.repo.showCommit(view.repo.commits.shaAt(currentIndex))
        }

        anchors.fill: parent
        clip: true
        focus: true
        model: view.repo.commits
        currentIndex: -1
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        keyNavigationEnabled: false
        ScrollBar.vertical: ScrollBar {}

        // Selection follows the user, not model resets, so a refresh never
        // replaces the diff on display.
        Keys.onUpPressed: {
            decrementCurrentIndex()
            showCurrent()
        }
        Keys.onDownPressed: {
            incrementCurrentIndex()
            showCurrent()
        }

        Connections {
            target: view.repo.commits
            function onModelReset() {
                list.currentIndex = view.repo.commits.rowOf(view.repo.diffCommit)
            }
        }

        delegate: CommitDelegate {
            width: ListView.view.width
            dropTarget: index === view.dropRow
            dropBlocker: dropTarget ? view.dropBlocker : ""
            suggested: sha === view.repo.suggestedFoldTarget
            onContextMenuRequested: {
                commitMenu.sha = sha
                commitMenu.blocker = view.repo.foldBlocker(sha)
                commitMenu.popup()
            }
            onClicked: {
                list.currentIndex = index
                list.forceActiveFocus()
                list.showCurrent()
            }
        }
    }
}
