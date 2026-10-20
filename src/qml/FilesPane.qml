pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core

// The repository's tracked files: click one to see its content, double-click
// (or right-click) for its time-lapse view.
Pane {
    id: pane

    required property Repository repo

    property string loadedHead: "-"
    readonly property string filter: filterField.text.trim()

    signal timelapseRequested(string path)

    function reload() {
        loadedHead = repo.headSha
        repo.loadFiles()
    }

    padding: 0
    onVisibleChanged: if (visible) reload()

    // New commits may add or remove files.
    Connections {
        target: pane.repo
        function onHeadChanged() {
            if (pane.visible && pane.repo.headSha !== pane.loadedHead)
                pane.reload()
        }
    }

    Menu {
        id: fileMenu

        property string path

        MenuItem {
            text: qsTr("Time-lapse View")
            icon.source: Icons.source("history")
            icon.color: palette.windowText
            onTriggered: pane.timelapseRequested(fileMenu.path)
        }
        MenuItem {
            text: qsTr("Copy Path")
            icon.source: Icons.source("copy")
            icon.color: palette.windowText
            onTriggered: Clipboard.setText(fileMenu.path)
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
                anchors.rightMargin: 6

                Icon {
                    name: "search"
                }
                TextField {
                    id: filterField
                    objectName: "fileFilter"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Filter files")
                    selectByMouse: true
                    Keys.onEscapePressed: clear()
                }
                Label {
                    text: pane.filter === "" ? qsTr("%n file(s)", "", pane.repo.files.fileCount)
                                             : qsTr("%n match(es)", "", matches.count)
                    opacity: 0.7
                }
            }
        }

        TreeView {
            id: tree

            objectName: "fileTree"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: pane.filter === ""
            clip: true
            model: pane.repo.files
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: TreeViewDelegate {
                id: entry

                required property string path
                required property bool isDir
                required property string fileName

                implicitWidth: tree.width
                highlighted: !entry.isDir && pane.repo.diffSource === Repository.FileContent
                             && pane.repo.diffPath === entry.path
                contentItem: RowLayout {
                    spacing: 6

                    Icon {
                        name: entry.isDir ? (entry.expanded ? "folder-open" : "folder") : "file"
                        color: entry.isDir ? "#c9a227" : entry.palette.text
                    }
                    Label {
                        Layout.fillWidth: true
                        text: entry.fileName
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                    }
                }

                TapHandler {
                    onTapped: {
                        if (entry.isDir)
                            tree.toggleExpanded(entry.row)
                        else
                            pane.repo.showFileContent(entry.path)
                    }
                    onDoubleTapped: if (!entry.isDir) pane.timelapseRequested(entry.path)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    enabled: !entry.isDir
                    onTapped: eventPoint => {
                        fileMenu.path = entry.path
                        fileMenu.popup(entry, eventPoint.position.x, eventPoint.position.y)
                    }
                }
            }
        }

        ListView {
            id: matches

            objectName: "fileMatches"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: pane.filter !== ""
            clip: true
            // Depends on fileCount so it follows reloads.
            model: pane.filter === "" || pane.repo.files.fileCount < 0 ? [] : pane.repo.files.match(pane.filter)
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: match

                required property string modelData

                width: ListView.view.width
                height: 26
                highlighted: pane.repo.diffSource === Repository.FileContent && pane.repo.diffPath === modelData
                onClicked: pane.repo.showFileContent(modelData)
                onDoubleClicked: pane.timelapseRequested(modelData)

                contentItem: RowLayout {
                    spacing: 6

                    Icon {
                        name: "file"
                    }
                    Label {
                        text: match.modelData.split("/").pop()
                        textFormat: Text.PlainText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: match.modelData.split("/").slice(0, -1).join("/")
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        opacity: 0.6
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        fileMenu.path = match.modelData
                        fileMenu.popup(match, eventPoint.position.x, eventPoint.position.y)
                    }
                }
            }
        }
    }
}
