pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core

Pane {
    id: view

    required property Repository repo

    property int dropRow: -1
    property string dropBlocker

    readonly property var columns: AppSettings.graph

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

    function updateSearch() {
        repo.commits.setSearch(search.text, matchCase.checked, regex.checked)
    }

    function findMatch(forward: bool) {
        if (!repo.commits.searching)
            return
        const from = list.currentIndex >= 0 ? list.currentIndex : (forward ? -1 : 0)
        const row = repo.commits.findNext(from, forward)
        if (row < 0)
            return
        list.currentIndex = row
        list.positionViewAtIndex(row, ListView.Contain)
        list.showCurrent()
    }

    padding: 0

    Menu {
        id: commitMenu

        property string sha
        property string subject
        property string blocker

        MenuItem {
            text: commitMenu.blocker === ""
                  ? qsTr("Fold %n selected line(s) into this commit", "", view.repo.diff.selectedCount)
                  : qsTr("Can't fold: %1").arg(commitMenu.blocker)
            enabled: commitMenu.blocker === ""
            icon.source: Icons.source("fold-vertical")
            icon.color: palette.windowText
            onTriggered: view.foldRequested(commitMenu.sha)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Copy Commit SHA")
            icon.source: Icons.source("copy")
            icon.color: palette.windowText
            onTriggered: Clipboard.setText(commitMenu.sha)
        }
        MenuItem {
            text: qsTr("Copy Subject")
            icon.source: Icons.source("copy")
            icon.color: palette.windowText
            onTriggered: Clipboard.setText(commitMenu.subject)
        }
    }

    Menu {
        id: viewMenu

        component Toggle: MenuItem {
            required property string setting
            checkable: true
            checked: view.columns[setting]
            onToggled: view.columns[setting] = checked
        }

        Toggle { text: qsTr("Branch / Tag"); setting: "showRefs" }
        Toggle { text: qsTr("Graph"); setting: "showGraph" }
        Toggle { text: qsTr("Changes"); setting: "showChanges" }
        Toggle { text: qsTr("Author"); setting: "showAuthor" }
        Toggle { text: qsTr("Date"); setting: "showDate" }
        Toggle { text: qsTr("SHA"); setting: "showSha" }
        MenuSeparator {}
        Toggle { text: qsTr("Remote Branches"); setting: "showRemotes" }
        Toggle { text: qsTr("Tags"); setting: "showTags" }
        Toggle { text: qsTr("Dim Merge Commits"); setting: "dimMerges" }
        Toggle { text: qsTr("Author Initials in Graph"); setting: "showInitials" }
        Toggle { text: qsTr("Activity Minimap"); setting: "showMinimap" }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6

                ComboBox {
                    objectName: "branchScope"
                    model: [qsTr("All Branches"), qsTr("Current Branch")]
                    currentIndex: view.columns.currentBranchOnly ? 1 : 0
                    onActivated: index => view.columns.currentBranchOnly = index === 1
                }
                TextField {
                    id: search
                    objectName: "historySearch"
                    Layout.preferredWidth: 380
                    placeholderText: qsTr("Search commits, e.g. \"fix crash\" author:ada sha:1a2b")
                    selectByMouse: true
                    onTextChanged: view.updateSearch()
                    onAccepted: view.findMatch(true)
                    Keys.onEscapePressed: clear()
                    ToolTip.visible: hovered && text === ""
                    ToolTip.delay: 800
                    ToolTip.text: qsTr("Terms must all match. author:, message: and sha: limit a term to "
                                       + "that field; quotes keep a phrase together.")
                }
                IconButton {
                    iconName: "case-sensitive"
                    id: matchCase
                    checkable: true
                    onToggled: view.updateSearch()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Match case")
                }
                IconButton {
                    iconName: "regex"
                    id: regex
                    checkable: true
                    onToggled: view.updateSearch()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Regular expressions")
                }
                Label {
                    visible: view.repo.commits.searching
                    text: view.repo.commits.searchError !== "" ? view.repo.commits.searchError
                        : view.repo.commits.matchCount === 0 ? qsTr("No results")
                        : qsTr("%n match(es)", "", view.repo.commits.matchCount)
                    color: view.repo.commits.searchError !== "" ? "#d62728" : view.palette.text
                    opacity: 0.8
                }
                IconButton {
                    iconName: "chevron-up"
                    enabled: view.repo.commits.matchCount > 0
                    onClicked: view.findMatch(false)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Previous match (Shift+Enter)")
                }
                IconButton {
                    iconName: "chevron-down"
                    enabled: view.repo.commits.matchCount > 0
                    onClicked: view.findMatch(true)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Next match (Enter)")
                }
                Item {
                    Layout.fillWidth: true
                }
                IconButton {
                    iconName: "columns-3"
                    text: qsTr("View")
                    onClicked: viewMenu.popup(this, 0, height)
                }
            }

            Shortcut {
                sequence: "Shift+Return"
                enabled: search.activeFocus
                onActivated: view.findMatch(false)
            }
        }

        ActivityMinimap {
            objectName: "minimap"
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            visible: view.columns.showMinimap
            model: view.repo.commits
            currentRow: list.currentIndex
            // Depend on contentY so the band follows scrolling.
            firstVisibleRow: list.contentY, list.indexAt(10, list.contentY + 2)
            lastVisibleRow: {
                const row = list.indexAt(10, list.contentY + list.height - 2)
                return row >= 0 ? row : list.count - 1
            }
            lineColor: view.palette.highlight
            bandColor: view.palette.highlight
            textColor: view.palette.placeholderText
            onRowRequested: row => {
                list.currentIndex = row
                list.positionViewAtIndex(row, ListView.Center)
                list.showCurrent()
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: view.palette.mid
            }
        }

        // Column headers; drag a right edge to resize, right-click to pick columns.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 24
            color: view.palette.button

            component ColumnHeader: Item {
                id: header

                property string title
                property real columnWidth
                property bool resizable: true

                signal resized(real width)

                Layout.fillHeight: true
                clip: true

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 6
                    width: parent.width - 12
                    text: header.title
                    elide: Text.ElideRight
                    font.bold: true
                    font.pixelSize: 11
                    opacity: 0.8
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: header.palette.mid
                }
                MouseArea {
                    property real startX
                    property real startWidth

                    visible: header.resizable
                    anchors.right: parent.right
                    width: 8
                    height: parent.height
                    cursorShape: Qt.SplitHCursor
                    onPressed: mouse => {
                        startX = mapToItem(null, mouse.x, 0).x
                        startWidth = header.columnWidth
                    }
                    onPositionChanged: mouse => header.resized(Math.max(40, startWidth + mapToItem(null, mouse.x, 0).x - startX))
                }
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                ColumnHeader {
                    visible: view.columns.showRefs
                    Layout.preferredWidth: view.columns.refsWidth
                    title: qsTr("Branch / Tag")
                    columnWidth: view.columns.refsWidth
                    onResized: width => view.columns.refsWidth = width
                }
                ColumnHeader {
                    visible: view.columns.showGraph
                    Layout.preferredWidth: view.columns.graphWidth
                    title: qsTr("Graph")
                    columnWidth: view.columns.graphWidth
                    onResized: width => view.columns.graphWidth = width
                }
                ColumnHeader {
                    Layout.fillWidth: true
                    title: qsTr("Commit Message")
                    resizable: false
                }
                ColumnHeader {
                    visible: view.columns.showChanges
                    Layout.preferredWidth: view.columns.changesWidth
                    title: qsTr("Changes")
                    columnWidth: view.columns.changesWidth
                    onResized: width => view.columns.changesWidth = width
                }
                ColumnHeader {
                    visible: view.columns.showAuthor
                    Layout.preferredWidth: view.columns.authorWidth
                    title: qsTr("Author")
                    columnWidth: view.columns.authorWidth
                    onResized: width => view.columns.authorWidth = width
                }
                ColumnHeader {
                    visible: view.columns.showDate
                    Layout.preferredWidth: view.columns.dateWidth
                    title: qsTr("Date")
                    columnWidth: view.columns.dateWidth
                    onResized: width => view.columns.dateWidth = width
                }
                ColumnHeader {
                    visible: view.columns.showSha
                    Layout.preferredWidth: view.columns.shaWidth
                    title: qsTr("SHA")
                    columnWidth: view.columns.shaWidth
                    onResized: width => view.columns.shaWidth = width
                }
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: eventPoint => viewMenu.popup(eventPoint.position.x, eventPoint.position.y)
            }
        }

        ListView {
            id: list

            function showCurrent() {
                if (currentIndex >= 0)
                    view.repo.showCommit(view.repo.commits.shaAt(currentIndex))
            }

            objectName: "historyList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: view.repo.commits
            currentIndex: -1
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            highlightMoveDuration: 0
            keyNavigationEnabled: false
            ScrollBar.vertical: ScrollBar {}

            header: WipRow {
                width: ListView.view.width
                repo: view.repo
            }

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
                repo: view.repo
                dropTarget: index === view.dropRow
                dropBlocker: dropTarget ? view.dropBlocker : ""
                suggested: sha === view.repo.suggestedFoldTarget
                onContextMenuRequested: position => {
                    commitMenu.sha = sha
                    commitMenu.subject = subject
                    commitMenu.blocker = view.repo.foldBlocker(sha)
                    commitMenu.popup(this, position.x, position.y)
                }
                onClicked: {
                    list.currentIndex = index
                    list.forceActiveFocus()
                    list.showCurrent()
                }
            }
        }
    }
}
