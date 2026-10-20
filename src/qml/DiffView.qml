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
    // A diff that ignores white space would not apply back.
    readonly property bool canStage: workingDiff && !repo.ignoreWhitespace
    readonly property bool showsDiff: workingDiff || repo.diffSource === Repository.CommitDiff

    // Emitted in scene coordinates while the selected lines are dragged
    // towards the history, and once when they are dropped.
    signal foldDragMoved(point scenePos)
    signal foldDragFinished(point scenePos)
    signal foldRequested(string sha)
    signal timelapseRequested(string path)

    readonly property bool hasSelection: canStage && repo.diff.selectedCount > 0

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
                        case Repository.PatchSetDiff:
                            return qsTr("Patch set %1 → commit %2").arg(view.repo.diffPatchSet)
                                .arg(view.repo.diffCommit.substring(0, 10))
                        default: return ""
                        }
                    }
                }
                Label {
                    visible: view.workingDiff && view.repo.diff.selectedCount === 0
                    text: view.canStage ? qsTr("Click lines to select, Shift+click for a range")
                                        : qsTr("White space is ignored, so lines cannot be staged")
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
                    objectName: "ignoreWhitespaceButton"
                    iconName: "space"
                    visible: view.showsDiff
                    checked: AppSettings.diff.ignoreWhitespace
                    onClicked: AppSettings.diff.ignoreWhitespace = !AppSettings.diff.ignoreWhitespace
                    ToolTip.visible: hovered
                    ToolTip.text: AppSettings.diff.ignoreWhitespace
                                  ? qsTr("Show changes of white space")
                                  : qsTr("Ignore changes of white space (lines cannot be staged then)")
                }
                IconButton {
                    objectName: "wordDiffButton"
                    iconName: "whole-word"
                    visible: view.showsDiff
                    checked: AppSettings.diff.wordDiff
                    onClicked: AppSettings.diff.wordDiff = !AppSettings.diff.wordDiff
                    ToolTip.visible: hovered
                    ToolTip.text: AppSettings.diff.wordDiff ? qsTr("Stop marking changed words")
                                                            : qsTr("Mark the words that changed")
                }
                IconButton {
                    objectName: "highlightButton"
                    iconName: "highlighter"
                    visible: view.repo.diffSource !== Repository.NoDiff
                    checked: AppSettings.diff.highlightSyntax
                    onClicked: AppSettings.diff.highlightSyntax = !AppSettings.diff.highlightSyntax
                    ToolTip.visible: hovered
                    ToolTip.text: AppSettings.diff.highlightSyntax ? qsTr("Stop highlighting syntax")
                                                                   : qsTr("Highlight syntax")
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
                    enabled: view.canStage && view.repo.diff.selectedCount > 0 && !view.repo.writing
                    text: view.stagedDiff ? qsTr("Unstage %n line(s)", "", view.repo.diff.selectedCount)
                                          : qsTr("Stage %n line(s)", "", view.repo.diff.selectedCount)
                    onClicked: view.repo.applySelection()
                }
            }
        }

        BoundedScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(details.implicitHeight + 12, view.height * (review.visible ? 0.25 : 0.35))
            visible: view.repo.diffSource === Repository.CommitDiff || view.repo.diffSource === Repository.PatchSetDiff

            TextArea {
                id: details
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.PlainText
                font: view.monoFont
                text: view.repo.commitDetails
            }
        }

        ReviewPanel {
            id: review

            objectName: "reviewPanel"
            Layout.fillWidth: true
            Layout.fillHeight: false // the diff gets what is left
            repo: view.repo
            monoFont: view.monoFont
            maxBodyHeight: view.height * 0.25
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
                required property var markup

                readonly property bool header: kind === DiffModel.FileHeader
                // Gerrit's inline comments on this line, on either side.
                readonly property var comments: {
                    const marks = view.repo.diffComments
                    if (kind < DiffModel.Context || kind > DiffModel.Removed || marks.newSide === undefined)
                        return []
                    const on = (side, line) => line > 0 ? side[`${path}:${line}`] ?? [] : []
                    return on(marks.oldSide, oldLine).concat(on(marks.newSide, newLine))
                }

                width: lines.contentWidth
                height: lineArea.height + (comments.length > 0 ? commentBox.implicitHeight : 0)
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

                Item {
                    id: lineArea
                    width: parent.width
                    height: row.header ? metrics.height + 12 : metrics.height + 2
                }

                // Shown below the line, as Gerrit does, without the
                // line's tint.
                Rectangle {
                    visible: commentBox.visible
                    y: lineArea.height
                    width: parent.width
                    height: parent.height - y
                    color: view.palette.base
                }
                Column {
                    id: commentBox

                    visible: row.comments.length > 0
                    x: lines.contentX + lines.gutterWidth
                    y: lineArea.height
                    width: Math.min(lines.width - lines.gutterWidth - 16, 720)
                    spacing: 2
                    bottomPadding: 4

                    Repeater {
                        model: row.comments

                        delegate: Rectangle {
                            id: lineComment

                            required property var modelData

                            width: commentBox.width
                            height: commentText.implicitHeight + 8
                            radius: 4
                            color: view.palette.toolTipBase
                            border.color: view.palette.mid

                            Icon {
                                id: commentIcon
                                x: 5
                                y: 5
                                name: "message-square"
                                size: 14
                                color: view.palette.toolTipText
                            }
                            Text {
                                id: commentText
                                x: commentIcon.x + commentIcon.width + 6
                                y: 4
                                width: parent.width - x - 6
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                                color: view.palette.toolTipText
                                text: `${lineComment.modelData.author}: ${lineComment.modelData.text}`
                            }
                        }
                    }
                }

                Text {
                    id: gutter
                    visible: row.kind >= DiffModel.Context && row.kind <= DiffModel.Removed
                    width: lines.gutterWidth
                    anchors.verticalCenter: lineArea.verticalCenter
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
                    anchors.verticalCenter: lineArea.verticalCenter
                    name: row.collapsed ? "chevron-right" : "chevron-down"
                }

                // Plain text, never markup, with the colors of its markup.
                DiffLineText {
                    id: lineText

                    readonly property string marker: {
                        switch (row.kind) {
                        case DiffModel.Added: return "+"
                        case DiffModel.Removed: return "-"
                        case DiffModel.Context: return view.repo.diffSource === Repository.FileContent ? "" : " "
                        case DiffModel.NoNewline: return "\\"
                        default: return ""
                        }
                    }

                    objectName: "lineText"
                    anchors.left: parent.left
                    anchors.leftMargin: row.header ? 28 : lines.gutterWidth
                    anchors.verticalCenter: lineArea.verticalCenter
                    font.family: view.monoFont.family
                    font.pointSize: view.monoFont.pointSize
                    font.bold: row.header
                    font.italic: row.kind === DiffModel.NoNewline || row.kind === DiffModel.Message
                    color: row.kind === DiffModel.HunkHeader ? view.palette.link : view.palette.text
                    text: marker + row.text
                    markupOffset: marker.length
                    markup: row.markup
                    highlight: AppSettings.diff.highlightSyntax
                    wordDiff: AppSettings.diff.wordDiff
                    dark: view.palette.base.hslLightness < 0.5
                    changeColor: row.kind === DiffModel.Added ? Qt.rgba(0.2, 0.7, 0.3, 0.4)
                                                              : Qt.rgba(0.85, 0.25, 0.25, 0.4)
                }

                // Lines added and removed in the file.
                Row {
                    visible: row.header && (row.added > 0 || row.removed > 0)
                    anchors.left: lineText.right
                    anchors.leftMargin: 12
                    anchors.verticalCenter: lineArea.verticalCenter
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
                    enabled: view.canStage
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
