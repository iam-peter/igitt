pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

Pane {
    id: view

    required property Repository repo
    required property font monoFont

    readonly property bool workingDiff: repo.diffSource === Repository.UnstagedDiff
                                        || repo.diffSource === Repository.StagedDiff
    readonly property bool stagedDiff: repo.diffSource === Repository.StagedDiff

    // Emitted in scene coordinates while the selected lines are dragged
    // towards the history, and once when they are dropped.
    signal foldDragMoved(point scenePos)
    signal foldDragFinished(point scenePos)
    signal foldRequested(string sha)
    signal timelapseRequested(string path)

    readonly property bool hasSelection: workingDiff && repo.diff.selectedCount > 0

    padding: 0

    FontMetrics {
        id: metrics
        font: view.monoFont
    }

    Menu {
        id: fileMenu

        property string path

        MenuItem {
            text: qsTr("Time-lapse View")
            icon.source: Icons.source("history")
            icon.color: palette.windowText
            onTriggered: view.timelapseRequested(fileMenu.path)
        }
    }

    // The git gui actions: stage or unstage the hunk or line under the
    // pointer.
    Menu {
        id: lineMenu

        property int row: -1
        property bool onLine: false
        property bool inSelection: false

        function show(row: int, onLine: bool, item: Item, x: real, y: real) {
            lineMenu.row = row
            lineMenu.onLine = onLine
            inSelection = onLine && view.repo.diff.isRowSelected(row) && view.repo.diff.selectedCount > 1
            popup(item, x, y)
        }

        MenuItem {
            text: view.stagedDiff ? qsTr("Unstage Hunk") : qsTr("Stage Hunk")
            enabled: !view.repo.writing
            onTriggered: view.repo.applyHunk(lineMenu.row)
        }
        MenuItem {
            text: {
                const n = view.repo.diff.selectedCount
                if (lineMenu.inSelection)
                    return view.stagedDiff ? qsTr("Unstage %n Selected Lines", "", n)
                                           : qsTr("Stage %n Selected Lines", "", n)
                return view.stagedDiff ? qsTr("Unstage Line") : qsTr("Stage Line")
            }
            enabled: lineMenu.onLine && !view.repo.writing
            onTriggered: view.repo.applyLine(lineMenu.row)
        }
    }

    Loader {
        anchors.fill: parent
        active: view.repo.diffConflicted
        sourceComponent: ConflictView {
            repo: view.repo
            path: view.repo.diffPath
            monoFont: view.monoFont
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: !view.repo.diffConflicted

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    font.bold: true
                    text: {
                        switch (view.repo.diffSource) {
                        case Repository.UnstagedDiff: return qsTr("Unstaged: %1").arg(view.repo.diffPath)
                        case Repository.StagedDiff: return qsTr("Staged: %1").arg(view.repo.diffPath)
                        case Repository.CommitDiff: return qsTr("Commit %1").arg(view.repo.diffCommit.substring(0, 10))
                        case Repository.FileContent: return qsTr("File: %1").arg(view.repo.diffPath)
                        default: return ""
                        }
                    }
                }
                Label {
                    visible: view.workingDiff && view.repo.diff.selectedCount === 0
                    text: qsTr("Click lines to select, Shift+click for a range")
                    opacity: 0.6
                }
                Rectangle {
                    id: foldHandle

                    property point lastScenePos

                    objectName: "foldHandle"
                    visible: view.hasSelection
                    implicitWidth: handleLabel.implicitWidth + 16
                    implicitHeight: handleLabel.implicitHeight + 8
                    radius: 4
                    color: foldDrag.active ? view.palette.midlight : view.palette.button
                    border.color: view.palette.mid

                    Row {
                        id: handleLabel
                        anchors.centerIn: parent
                        spacing: 4

                        Icon {
                            name: "grip-vertical"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Label {
                            text: qsTr("Drag onto a commit to fold")
                        }
                    }

                    HoverHandler {
                        id: handleHover
                        cursorShape: Qt.OpenHandCursor
                    }
                    ToolTip.visible: handleHover.hovered && !foldDrag.active
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Drop on a commit in the history to fold the selected lines into it. "
                                       + "Right-clicking a commit works too.")

                    DragHandler {
                        id: foldDrag
                        target: null
                        cursorShape: Qt.ClosedHandCursor
                        onCentroidChanged: {
                            if (!active)
                                return
                            foldHandle.lastScenePos = centroid.scenePosition
                            view.foldDragMoved(foldHandle.lastScenePos)
                        }
                        onActiveChanged: {
                            if (!active)
                                view.foldDragFinished(foldHandle.lastScenePos)
                        }
                    }

                    // Follows the pointer while dragging.
                    Label {
                        parent: Overlay.overlay
                        visible: foldDrag.active
                        x: foldHandle.lastScenePos.x + 14
                        y: foldHandle.lastScenePos.y + 10
                        padding: 4
                        text: qsTr("Fold %n line(s)", "", view.repo.diff.selectedCount)
                        background: Rectangle {
                            color: view.palette.toolTipBase
                            border.color: view.palette.mid
                            radius: 3
                        }
                    }
                }
                IconButton {
                    objectName: "fileTimelapseButton"
                    iconName: "history"
                    text: qsTr("Time-lapse")
                    visible: view.repo.diffSource === Repository.FileContent
                    onClicked: view.timelapseRequested(view.repo.diffPath)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("See this file through its history")
                }
                IconButton {
                    objectName: "collapseAllButton"
                    iconName: "chevrons-down-up"
                    visible: view.repo.diff.fileCount > 1
                    enabled: view.repo.diff.collapsedCount < view.repo.diff.fileCount
                    onClicked: view.repo.diff.setAllCollapsed(true)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Collapse all files")
                }
                IconButton {
                    objectName: "expandAllButton"
                    iconName: "chevrons-up-down"
                    visible: view.repo.diff.fileCount > 1
                    enabled: view.repo.diff.collapsedCount > 0
                    onClicked: view.repo.diff.setAllCollapsed(false)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Expand all files")
                }
                IconButton {
                    iconName: "fold-vertical"
                    objectName: "foldSuggestedButton"
                    visible: view.hasSelection && view.repo.suggestedFoldTarget !== ""
                    text: qsTr("Fold into suggested")
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: view.repo.foldSuggestionText
                    onClicked: view.foldRequested(view.repo.suggestedFoldTarget)
                }
                Label {
                    Layout.maximumWidth: 380
                    visible: view.hasSelection && view.repo.suggestedFoldTarget === ""
                             && view.repo.foldSuggestionText !== ""
                    text: view.repo.foldSuggestionText
                    elide: Text.ElideRight
                    opacity: 0.6
                }
                IconButton {
                    iconName: view.stagedDiff ? "undo-2" : "plus"
                    visible: view.workingDiff
                    enabled: view.repo.diff.selectedCount > 0 && !view.repo.writing
                    text: view.stagedDiff ? qsTr("Unstage %n line(s)", "", view.repo.diff.selectedCount)
                                          : qsTr("Stage %n line(s)", "", view.repo.diff.selectedCount)
                    onClicked: view.repo.applySelection()
                }
            }
        }

        BoundedScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(details.implicitHeight + 12, view.height * 0.35)
            visible: view.repo.diffSource === Repository.CommitDiff

            TextArea {
                id: details
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.PlainText
                font: view.monoFont
                text: view.repo.commitDetails
            }
        }

        ListView {
            id: lines

            property int anchorRow: -1

            objectName: "diffList"
            // A file has one line number per line, a diff two (old and new).
            readonly property bool singleNumbers: view.repo.diffSource === Repository.FileContent
            readonly property int numberWidth: singleNumbers ? Math.max(3, String(count).length) : 5
            readonly property real gutterWidth: metrics.averageCharacterWidth
                                                * (singleNumbers ? numberWidth + 2 : 2 * numberWidth + 2) + 8

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: view.repo.diff
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            flickableDirection: Flickable.AutoFlickIfNeeded
            contentWidth: Math.max(width, gutterWidth + view.repo.diff.maxLineLength
                                          * metrics.averageCharacterWidth + 24)
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}

            delegate: Rectangle {
                id: row

                required property int index
                required property int kind
                required property string text
                required property int oldLine
                required property int newLine
                required property bool selectable
                required property bool selected
                required property string path
                required property bool collapsed
                required property int added
                required property int removed

                readonly property bool header: kind === DiffModel.FileHeader

                width: lines.contentWidth
                height: header ? metrics.height + 12 : metrics.height + 2
                color: {
                    if (selected)
                        return Qt.alpha(view.palette.highlight, 0.55)
                    switch (kind) {
                    case DiffModel.FileHeader: return view.palette.button
                    case DiffModel.HunkHeader: return Qt.rgba(0.3, 0.45, 0.8, 0.15)
                    case DiffModel.Added: return Qt.rgba(0.2, 0.7, 0.3, 0.18)
                    case DiffModel.Removed: return Qt.rgba(0.85, 0.25, 0.25, 0.18)
                    default: return "transparent"
                    }
                }

                Text {
                    id: gutter
                    visible: row.kind >= DiffModel.Context && row.kind <= DiffModel.Removed
                    width: lines.gutterWidth
                    anchors.verticalCenter: parent.verticalCenter
                    font: view.monoFont
                    color: view.palette.placeholderText
                    text: {
                        const number = n => (n > 0 ? String(n) : "").padStart(lines.numberWidth)
                        return lines.singleNumbers ? number(row.newLine)
                                                   : number(row.oldLine) + " " + number(row.newLine)
                    }
                }

                Icon {
                    visible: row.header
                    x: 6
                    anchors.verticalCenter: parent.verticalCenter
                    name: row.collapsed ? "chevron-right" : "chevron-down"
                }

                Text {
                    id: lineText
                    anchors.left: parent.left
                    anchors.leftMargin: row.header ? 28 : lines.gutterWidth
                    anchors.verticalCenter: parent.verticalCenter
                    font.family: view.monoFont.family
                    font.pointSize: view.monoFont.pointSize
                    font.bold: row.header
                    font.italic: row.kind === DiffModel.NoNewline || row.kind === DiffModel.Message
                    textFormat: Text.PlainText
                    color: row.kind === DiffModel.HunkHeader ? view.palette.link : view.palette.text
                    text: {
                        switch (row.kind) {
                        case DiffModel.Added: return "+" + row.text
                        case DiffModel.Removed: return "-" + row.text
                        case DiffModel.Context:
                            return (view.repo.diffSource === Repository.FileContent ? "" : " ") + row.text
                        case DiffModel.NoNewline: return "\\" + row.text
                        default: return row.text
                        }
                    }
                }

                // Lines added and removed in the file.
                Row {
                    visible: row.header && (row.added > 0 || row.removed > 0)
                    anchors.left: lineText.right
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    Label {
                        visible: row.added > 0
                        text: `+${row.added}`
                        color: "#3c8c3c"
                        font.bold: true
                    }
                    Label {
                        visible: row.removed > 0
                        text: `−${row.removed}`
                        color: "#c0392b"
                        font.bold: true
                    }
                }

                TapHandler {
                    enabled: row.header
                    onTapped: view.repo.diff.toggleFile(row.index)
                }
                HoverHandler {
                    enabled: row.header
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    enabled: row.header && row.path !== ""
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        fileMenu.path = row.path
                        fileMenu.popup(row, eventPoint.position.x, eventPoint.position.y)
                    }
                }

                MouseArea {
                    readonly property bool clickable: row.selectable || row.kind === DiffModel.HunkHeader

                    anchors.fill: parent
                    enabled: view.workingDiff
                             && row.kind >= DiffModel.HunkHeader && row.kind <= DiffModel.NoNewline
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: enabled && clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton) {
                            lineMenu.show(row.index, row.selectable, this, mouse.x, mouse.y)
                        } else if (!clickable) {
                            return
                        } else if (row.kind === DiffModel.HunkHeader) {
                            view.repo.diff.toggleHunk(row.index)
                        } else if ((mouse.modifiers & Qt.ShiftModifier) && lines.anchorRow >= 0) {
                            view.repo.diff.selectRange(lines.anchorRow, row.index)
                        } else {
                            view.repo.diff.toggleLine(row.index)
                            lines.anchorRow = row.index
                        }
                    }
                }
            }

            Connections {
                target: view.repo.diff
                function onModelReset() {
                    lines.anchorRow = -1
                }
            }
        }
    }
}
