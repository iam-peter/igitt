pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core
import IgittStyle as Styles

Item {
    id: fileList

    required property Repository repo
    required property bool staged
    required property string title

    // Set by the pane while a file from the other list is dragged over
    // this one.
    property bool dropTarget: false
    // While a file is dragged over a commit to fold into: its subject, or
    // why it cannot take the fold
    property string foldInto
    property string foldRefused

    readonly property FileListModel files: staged ? repo.staged : repo.unstaged

    // In scene coordinates, while a file is dragged out of this list and
    // where it is let go.
    signal fileDragMoved(string path, point scenePos)
    signal fileDropped(string path, point scenePos)
    signal timelapseRequested(string path)
    // Files' unstaged changes to throw away, after asking
    signal discardRequested(var paths)
    // An untracked file to ignore, after asking how
    signal ignoreRequested(string path)
    // Files' changes to stash, after asking which
    signal stashRequested(var paths)
    // Every untracked file deleted, after showing which
    signal cleanRequested()

    // The files picked with Ctrl+click and Shift+click, for the menu to
    // act on together; the one clicked last is where a range starts
    property var selected: []
    property string anchor

    function toggleStage(path: string) {
        if (staged)
            repo.unstageFile(path)
        else
            repo.stageFile(path)
    }
    function click(path: string, row: int) {
        const modifiers = Input.modifiers()
        if (modifiers & Qt.ControlModifier) {
            selected = selected.includes(path) ? selected.filter(p => p !== path) : selected.concat([path])
            anchor = path
        } else if ((modifiers & Qt.ShiftModifier) && anchor !== "") {
            let from = -1
            for (let r = 0; r < files.count && from < 0; ++r) {
                if (files.pathAt(r) === anchor)
                    from = r
            }
            const range = []
            for (let r = Math.min(from < 0 ? row : from, row); r <= Math.max(from, row); ++r)
                range.push(files.pathAt(r))
            selected = range
        } else {
            selected = [path]
            anchor = path
        }
        repo.showFile(path, staged)
    }
    // What the menu acts on: the selection when the file is part of it
    function targets(path: string): var {
        return selected.length > 1 && selected.includes(path) ? selected : [path]
    }

    // Files no longer listed are no longer selected
    Connections {
        target: fileList.files
        function onModelReset() {
            const listed = new Set()
            for (let r = 0; r < fileList.files.count; ++r)
                listed.add(fileList.files.pathAt(r))
            fileList.selected = fileList.selected.filter(p => listed.has(p))
        }
    }

    function containsScenePoint(scenePos: point): bool {
        const local = mapFromItem(null, scenePos.x, scenePos.y)
        return local.x >= 0 && local.y >= 0 && local.x < width && local.y < height
    }

    Menu {
        id: fileMenu

        property string path
        property bool untracked
        property bool conflicted
        property var paths: []
        readonly property bool several: paths.length > 1

        MenuItem {
            objectName: fileList.staged ? "unstageFileMenuItem" : "stageFileMenuItem"
            text: fileMenu.several ? (fileList.staged ? qsTr("Unstage %n Files", "", fileMenu.paths.length)
                                                      : qsTr("Stage %n Files", "", fileMenu.paths.length))
                                   : fileList.staged ? qsTr("Unstage") : qsTr("Stage")
            enabled: !fileList.repo.writing
            onTriggered: fileList.staged ? fileList.repo.unstageFiles(fileMenu.paths)
                                         : fileList.repo.stageFiles(fileMenu.paths)
        }
        MenuItem {
            text: fileList.staged ? qsTr("Unstage All") : qsTr("Stage All")
            enabled: !fileList.repo.writing
            onTriggered: fileList.staged ? fileList.repo.unstageAll() : fileList.repo.stageAll()
        }
        MenuItem {
            objectName: "discardFileMenuItem"
            visible: !fileList.staged
            height: visible ? implicitHeight : 0
            text: fileList.repo.planning ? qsTr("Discard Changes (off while planning)")
                  : fileMenu.several ? qsTr("Discard the Changes to %n Files…", "", fileMenu.paths.length)
                  : qsTr("Discard Changes…")
            enabled: !fileList.repo.writing && !fileList.repo.planning
            icon.source: Icons.source("trash-2")
            icon.color: palette.windowText
            onTriggered: fileList.discardRequested(fileMenu.paths)
        }
        MenuItem {
            objectName: "stashFileMenuItem"
            text: fileList.repo.planning ? qsTr("Stash (off while planning)")
                  : fileMenu.several ? qsTr("Stash %n Files…", "", fileMenu.paths.length) : qsTr("Stash…")
            enabled: !fileList.repo.writing && !fileList.repo.planning
            icon.source: Icons.source("archive")
            icon.color: palette.windowText
            onTriggered: fileList.stashRequested(fileMenu.paths)
        }
        MenuItem {
            objectName: "ignoreFileMenuItem"
            visible: fileMenu.untracked && !fileMenu.several
            height: visible ? implicitHeight : 0
            text: qsTr("Ignore…")
            enabled: !fileList.repo.writing
            icon.source: Icons.source("square-dashed")
            icon.color: palette.windowText
            onTriggered: fileList.ignoreRequested(fileMenu.path)
        }
        MenuItem {
            objectName: "cleanMenuItem"
            visible: !fileList.staged
            height: visible ? implicitHeight : 0
            text: fileList.repo.planning ? qsTr("Delete Untracked Files (off while planning)")
                                         : qsTr("Delete Untracked Files…")
            enabled: !fileList.repo.writing && !fileList.repo.planning
            icon.source: Icons.source("trash-2")
            icon.color: palette.windowText
            onTriggered: fileList.cleanRequested()
        }
        MenuSeparator {}
        MenuItem {
            objectName: "mergeToolMenuItem"
            visible: fileMenu.conflicted && !fileMenu.several
            height: visible ? implicitHeight : 0
            text: qsTr("Open in Merge Tool")
            icon.source: Icons.source("git-merge")
            icon.color: palette.windowText
            onTriggered: fileList.repo.openMergeTool(fileMenu.path)
        }
        MenuItem {
            objectName: "diffToolMenuItem"
            visible: !fileMenu.untracked && !fileMenu.conflicted && !fileMenu.several
            height: visible ? implicitHeight : 0
            text: qsTr("Open in Diff Tool")
            icon.source: Icons.source("external-link")
            icon.color: palette.windowText
            onTriggered: fileList.repo.openChangeInDiffTool(fileMenu.path, fileList.staged)
        }
        MenuItem {
            text: qsTr("Time-lapse View")
            icon.source: Icons.source("history")
            icon.color: palette.windowText
            onTriggered: fileList.timelapseRequested(fileMenu.path)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6

                Label {
                    Layout.fillWidth: true
                    text: `${fileList.title} (${fileList.files.count})`
                    font.bold: true
                    elide: Text.ElideRight
                }
                IconButton {
                    iconName: fileList.staged ? "undo-2" : "plus"
                    text: fileList.staged ? qsTr("Unstage all") : qsTr("Stage all")
                    enabled: fileList.files.count > 0
                    onClicked: fileList.staged ? fileList.repo.unstageAll() : fileList.repo.stageAll()
                }
            }
        }

        ListView {
            objectName: fileList.staged ? "stagedList" : "unstagedList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: fileList.files
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            // Dragging with the mouse moves files, not the list; the wheel
            // and the scroll bar still scroll.
            acceptedButtons: Qt.NoButton
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: item

                required property string path
                required property string origPath
                required property string status
                required property bool untracked
                required property bool conflicted
                required property int index

                property point dragPos

                width: ListView.view.width
                height: 26
                // The style's padding leaves less than the status badge
                // needs, and a layout that does not fit starts at its top
                topPadding: 4
                bottomPadding: 4
                highlighted: (fileList.selected.length > 1 && fileList.selected.includes(path))
                             || (fileList.repo.diffPath === path
                                 && fileList.repo.diffSource === (fileList.staged ? Repository.StagedDiff
                                                                                  : Repository.UnstagedDiff))
                ToolTip.visible: hovered && origPath.length > 0 && !drag.active
                ToolTip.text: qsTr("renamed from %1").arg(origPath)
                ToolTip.delay: 600

                onClicked: fileList.click(path, index)
                onDoubleClicked: fileList.toggleStage(path)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        fileMenu.path = item.path
                        fileMenu.untracked = item.untracked
                        fileMenu.conflicted = item.conflicted
                        fileMenu.paths = fileList.targets(item.path)
                        // Placed in the list, which stays when rows are
                        // made again
                        const at = item.mapToItem(fileList, eventPoint.position.x, eventPoint.position.y)
                        fileMenu.popup(fileList, at.x, at.y)
                    }
                }

                DragHandler {
                    id: drag
                    target: null
                    // Not under a dialog, menu or popup (ApplicationWindow.popupOpen).
                    enabled: !(parent.Window.window?.popupOpen ?? false)
                    cursorShape: Qt.ClosedHandCursor
                    onCentroidChanged: {
                        if (!active)
                            return
                        item.dragPos = centroid.scenePosition
                        fileList.fileDragMoved(item.path, item.dragPos)
                    }
                    onActiveChanged: {
                        if (!active)
                            fileList.fileDropped(item.path, item.dragPos)
                    }
                }

                // Follows the pointer while dragging.
                Label {
                    parent: Overlay.overlay
                    visible: drag.active
                    x: item.dragPos.x + 14
                    y: item.dragPos.y + 10
                    padding: 4
                    objectName: "fileDragLabel"
                    // The reason after a colon, as part of the sentence
                    text: fileList.foldRefused !== ""
                          ? qsTr("Can't fold %1: %2").arg(item.path)
                                .arg(fileList.foldRefused.charAt(0).toLowerCase() + fileList.foldRefused.slice(1))
                        : fileList.foldInto !== "" ? qsTr("Fold %1 into “%2”").arg(item.path).arg(fileList.foldInto)
                        : (fileList.staged ? qsTr("Unstage %1") : qsTr("Stage %1")).arg(item.path)
                    textFormat: Text.PlainText
                    background: Rectangle {
                        color: item.palette.toolTipBase
                        border.color: item.palette.mid
                        radius: 3
                    }
                }

                contentItem: RowLayout {
                    spacing: 8

                    Rectangle {
                        id: badge
                        // Click the status badge to stage or unstage, like git gui.
                        implicitWidth: 18
                        implicitHeight: 18
                        radius: 3
                        color: Styles.TokenInterface.semantics.fileStatus(item.status)
                        Label {
                            anchors.centerIn: parent
                            text: item.status === "?" ? "+" : item.status
                            color: Styles.TokenInterface.textOn(badge.color)
                            font.bold: true
                            font.pixelSize: 11
                        }
                        TapHandler {
                            onTapped: fileList.toggleStage(item.path)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: item.path
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: item.palette.text
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: fileList.dropTarget
        color: Qt.alpha(Styles.TokenInterface.semantics.success_default, 0.15)
        border.width: 2
        border.color: Styles.TokenInterface.semantics.success_default
    }
}
