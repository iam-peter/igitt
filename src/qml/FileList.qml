pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

ColumnLayout {
    id: fileList

    required property Repository repo
    required property bool staged
    required property string title

    readonly property FileListModel files: staged ? repo.staged : repo.unstaged

    spacing: 0

    function toggleStage(path: string) {
        if (staged)
            repo.unstageFile(path)
        else
            repo.stageFile(path)
    }

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
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: fileList.files
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            id: item

            required property string path
            required property string origPath
            required property string status

            width: ListView.view.width
            height: 26
            highlighted: fileList.repo.diffPath === path
                         && fileList.repo.diffSource === (fileList.staged ? Repository.StagedDiff
                                                                          : Repository.UnstagedDiff)
            ToolTip.visible: hovered && origPath.length > 0
            ToolTip.text: qsTr("renamed from %1").arg(origPath)
            ToolTip.delay: 600

            onClicked: fileList.repo.showFile(path, fileList.staged)
            onDoubleClicked: fileList.toggleStage(path)

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
