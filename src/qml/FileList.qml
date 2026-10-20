pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

Item {
    id: fileList

    required property Repository repo
    required property bool staged
    required property string title

    // Set by the pane while a file from the other list is dragged over
    // this one.
    property bool dropTarget: false

    readonly property FileListModel files: staged ? repo.staged : repo.unstaged

    // In scene coordinates, while a file is dragged out of this list and
    // where it is let go.
    signal fileDragMoved(string path, point scenePos)
    signal fileDropped(string path, point scenePos)
    signal timelapseRequested(string path)

    function toggleStage(path: string) {
        if (staged)
            repo.unstageFile(path)
        else
            repo.stageFile(path)
    }

    function containsScenePoint(scenePos: point): bool {
        const local = mapFromItem(null, scenePos.x, scenePos.y)
        return local.x >= 0 && local.y >= 0 && local.x < width && local.y < height
    }

    Menu {
        id: fileMenu

        property string path

        MenuItem {
            objectName: fileList.staged ? "unstageFileMenuItem" : "stageFileMenuItem"
            text: fileList.staged ? qsTr("Unstage") : qsTr("Stage")
            enabled: !fileList.repo.writing
            onTriggered: fileList.toggleStage(fileMenu.path)
        }
        MenuItem {
            text: fileList.staged ? qsTr("Unstage All") : qsTr("Stage All")
            enabled: !fileList.repo.writing
            onTriggered: fileList.staged ? fileList.repo.unstageAll() : fileList.repo.stageAll()
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Time-lapse View")
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
                ToolButton {
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
            // Dragging with the mouse moves files, not the list; the wheel
            // and the scroll bar still scroll.
            acceptedButtons: Qt.NoButton
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: item

                required property string path
                required property string origPath
                required property string status

                property point dragPos

                width: ListView.view.width
                height: 26
                highlighted: fileList.repo.diffPath === path
                             && fileList.repo.diffSource === (fileList.staged ? Repository.StagedDiff
                                                                              : Repository.UnstagedDiff)
                ToolTip.visible: hovered && origPath.length > 0 && !drag.active
                ToolTip.text: qsTr("renamed from %1").arg(origPath)
                ToolTip.delay: 600

                onClicked: fileList.repo.showFile(path, fileList.staged)
                onDoubleClicked: fileList.toggleStage(path)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        fileMenu.path = item.path
                        fileMenu.popup(item, eventPoint.position.x, eventPoint.position.y)
                    }
                }

                DragHandler {
                    id: drag
                    target: null
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
                    text: (fileList.staged ? qsTr("Unstage %1") : qsTr("Stage %1")).arg(item.path)
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
                        // Click the status badge to stage or unstage, like git gui.
                        implicitWidth: 18
                        implicitHeight: 18
                        radius: 3
                        color: {
                            switch (item.status) {
                            case "A": case "?": return "#59a14f"
                            case "D": return "#e15759"
                            case "R": case "C": return "#4e79a7"
                            case "U": return "#d62728"
                            default: return "#e08e2b"
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            text: item.status === "?" ? "+" : item.status
                            color: "white"
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
                        color: item.highlighted ? item.palette.highlightedText : item.palette.text
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: fileList.dropTarget
        color: Qt.rgba(0.2, 0.7, 0.3, 0.15)
        border.width: 2
        border.color: "#59a14f"
    }
}
