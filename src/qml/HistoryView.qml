pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core
import IgittStyle as Styles

Pane {
    id: view

    required property Repository repo

    property int dropRow: -1
    property string dropBlocker
    // The commit being dragged to another place.
    property string movingSha

    readonly property var columns: AppSettings.graph

    signal foldRequested(string sha)
    // A history edit, see Repository.editCommit().
    signal editRequested(string kind, string sha, string to)
    signal rewordRequested(string sha)
    // The commit, not pushed yet, to rework in the Compose mode
    signal composeRequested(string sha)
    // Branches: one to make at a commit, to switch to, to rename, to delete
    signal branchRequested(string sha, string subject)
    signal checkoutRequested(string name)
    signal renameBranchRequested(string name)
    signal deleteBranchRequested(string name)
    // Another branch merged into the current one, or the current one
    // rebased onto it
    signal mergeRequested(string name)
    signal rebaseRequested(string name)
    // A commit picked onto the current branch, undone by a new one, or the
    // current branch reset to it
    signal cherryPickRequested(string sha)
    signal revertRequested(string sha)
    signal resetRequested(string sha, string subject)

    // Selects the commit and shows it, as if clicked
    function select(sha: string) {
        const row = repo.commits.rowOf(sha)
        if (row < 0)
            return
        list.currentIndex = row
        list.positionViewAtIndex(row, ListView.Contain)
        list.showCurrent()
    }

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

    function updateMove(sha: string, scenePos: point) {
        movingSha = sha
        let row = rowAt(scenePos)
        if (row >= 0 && repo.commits.shaAt(row) === sha)
            row = -1
        if (row === dropRow)
            return
        dropBlocker = row >= 0 ? repo.editBlocker("move", sha, repo.commits.shaAt(row)) : ""
        dropRow = row
    }

    function finishMove(sha: string, scenePos: point) {
        updateMove(sha, scenePos)
        const to = dropRow >= 0 && dropBlocker === "" ? repo.commits.shaAt(dropRow) : ""
        dropRow = -1
        dropBlocker = ""
        movingSha = ""
        if (to !== "")
            editRequested("move", sha, to)
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

    // A branch label's: switch to it, rename or delete it; a
    // remote-tracking one makes a local branch to switch to
    Menu {
        id: refMenu

        property string name
        property string kind
        property bool current

        MenuItem {
            objectName: "checkoutMenuItem"
            text: (refMenu.kind === "remote" ? qsTr("Check Out as a Local Branch") : qsTr("Check Out"))
                  + (view.repo.planning ? qsTr(" (off while planning)") : "")
            enabled: !refMenu.current && !view.repo.planning
            icon.source: Icons.source("git-branch")
            icon.color: palette.windowText
            onTriggered: view.checkoutRequested(refMenu.name)
        }
        MenuItem {
            objectName: "mergeBranchMenuItem"
            readonly property string branch: view.repo.branch
            text: view.repo.planning ? qsTr("Merge (off while planning)")
                                     : qsTr("Merge into %1").arg(branch)
            enabled: !refMenu.current && !view.repo.planning && branch !== "" && view.repo.operation === ""
            icon.source: Icons.source("git-merge")
            icon.color: palette.windowText
            onTriggered: view.mergeRequested(refMenu.name)
        }
        MenuItem {
            objectName: "rebaseBranchMenuItem"
            readonly property string branch: view.repo.branch
            text: view.repo.planning ? qsTr("Rebase (off while planning)")
                                     : qsTr("Rebase %1 onto This").arg(branch)
            enabled: !refMenu.current && !view.repo.planning && branch !== "" && view.repo.operation === ""
            icon.source: Icons.source("git-branch")
            icon.color: palette.windowText
            onTriggered: view.rebaseRequested(refMenu.name)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "renameBranchMenuItem"
            visible: refMenu.kind === "branch"
            height: visible ? implicitHeight : 0
            text: view.repo.planning ? qsTr("Rename (off while planning)") : qsTr("Rename…")
            enabled: !view.repo.planning
            icon.source: Icons.source("pencil")
            icon.color: palette.windowText
            onTriggered: view.renameBranchRequested(refMenu.name)
        }
        MenuItem {
            objectName: "deleteBranchMenuItem"
            visible: refMenu.kind === "branch"
            height: visible ? implicitHeight : 0
            text: view.repo.planning ? qsTr("Delete (off while planning)") : qsTr("Delete…")
            enabled: !refMenu.current && !view.repo.planning
            icon.source: Icons.source("trash-2")
            icon.color: palette.windowText
            onTriggered: view.deleteBranchRequested(refMenu.name)
            ToolTip.visible: hovered && refMenu.current
            ToolTip.text: qsTr("Switch to another branch first")
        }
    }

    Menu {
        id: commitMenu

        property string sha
        property string subject
        property string blocker

        MenuItem {
            objectName: "createBranchMenuItem"
            text: view.repo.planning ? qsTr("Create Branch Here (off while planning)") : qsTr("Create Branch Here…")
            enabled: !view.repo.planning
            icon.source: Icons.source("git-branch")
            icon.color: palette.windowText
            onTriggered: view.branchRequested(commitMenu.sha, commitMenu.subject)
        }
        MenuItem {
            objectName: "cherryPickMenuItem"
            readonly property bool off: view.repo.planning || view.repo.operation !== ""
            text: view.repo.planning ? qsTr("Cherry-Pick (off while planning)")
                                     : qsTr("Cherry-Pick onto %1").arg(view.repo.branch || "HEAD")
            enabled: !off && !view.repo.writing
            icon.source: Icons.source("git-commit-horizontal")
            icon.color: palette.windowText
            onTriggered: view.cherryPickRequested(commitMenu.sha)
        }
        MenuItem {
            objectName: "revertMenuItem"
            text: view.repo.planning ? qsTr("Revert (off while planning)") : qsTr("Revert with a New Commit")
            enabled: !view.repo.planning && view.repo.operation === "" && !view.repo.writing
            icon.source: Icons.source("undo-2")
            icon.color: palette.windowText
            onTriggered: view.revertRequested(commitMenu.sha)
        }
        MenuItem {
            objectName: "resetMenuItem"
            text: view.repo.planning ? qsTr("Reset (off while planning)")
                                     : qsTr("Reset %1 to Here…").arg(view.repo.branch || "HEAD")
            enabled: !view.repo.planning && view.repo.operation === "" && !view.repo.writing
            icon.source: Icons.source("step-back")
            icon.color: palette.windowText
            onTriggered: view.resetRequested(commitMenu.sha, commitMenu.subject)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "composeMenuItem"
            text: qsTr("Edit in Compose")
            enabled: commitMenu.visible && !view.repo.isPublished(commitMenu.sha)
            icon.source: Icons.source("boxes")
            icon.color: palette.windowText
            onTriggered: view.composeRequested(commitMenu.sha)
            ToolTip.visible: hovered && !enabled
            ToolTip.text: qsTr("A remote branch has it; changing it would need a force push")
            ToolTip.delay: 300
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
                    styles: [Styles.FieldStyle.medium]
                    objectName: "branchScope"
                    model: [qsTr("All Branches"), qsTr("Current Branch")]
                    currentIndex: view.columns.currentBranchOnly ? 1 : 0
                    onActivated: index => view.columns.currentBranchOnly = index === 1
                }
                TextField {
                    id: search
                    styles: [Styles.FieldStyle.medium]
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
                    styles: [Styles.ButtonStyle.medium]
                    iconName: "case-sensitive"
                    id: matchCase
                    checkable: true
                    onToggled: view.updateSearch()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Match case")
                }
                IconButton {
                    styles: [Styles.ButtonStyle.medium]
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
                    color: view.repo.commits.searchError !== "" ? Styles.TokenInterface.semantics.danger_default : view.palette.text
                    opacity: 0.8
                }
                IconButton {
                    styles: [Styles.ButtonStyle.medium]
                    iconName: "chevron-up"
                    enabled: view.repo.commits.matchCount > 0
                    onClicked: view.findMatch(false)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Previous match (Shift+Enter)")
                }
                IconButton {
                    styles: [Styles.ButtonStyle.medium]
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
                    styles: [Styles.ButtonStyle.medium]
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
            currentColor: Styles.TokenInterface.semantics.success_default
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
            // Dragging with the mouse moves commits, not the list; the wheel
            // and the scroll bar still scroll.
            acceptedButtons: Qt.NoButton
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
                dropHint: view.movingSha !== "" ? qsTr("Move it here") : qsTr("Fold into \"%1\"").arg(subject)
                suggested: sha === view.repo.suggestedFoldTarget
                onContextMenuRequested: position => {
                    commitMenu.sha = sha
                    commitMenu.subject = subject
                    commitMenu.blocker = view.repo.foldBlocker(sha)
                    commitMenu.popup(this, position.x, position.y)
                }
                // History edits are Compose's
                movable: false
                onRefMenuRequested: (name, kind, current, position) => {
                    refMenu.name = name
                    refMenu.kind = kind
                    refMenu.current = current
                    refMenu.popup(this, position.x, position.y)
                }
                onRefActivated: (name, kind, current) => {
                    if (!current && !view.repo.planning)
                        view.checkoutRequested(name)
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
