pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Igitt.Core

ApplicationWindow {
    id: root

    required property string initialPath
    required property font monoFont

    width: 1400
    height: 900
    visible: true
    title: repo.valid ? `${repo.path} — igitt` : "igitt"

    Repository {
        id: repo
        onErrorOccurred: message => errorBar.show(message)
    }

    Component.onCompleted: repo.open(root.initialPath)

    // Pick up changes made in a terminal or editor when the window is
    // activated again.
    Connections {
        target: Application
        function onStateChanged() {
            if (Application.state === Qt.ApplicationActive)
                repo.refreshStatus()
        }
    }

    FolderDialog {
        id: openDialog
        title: qsTr("Open Repository")
        onAccepted: repo.open(selectedFolder)
    }

    Shortcut {
        sequences: [StandardKey.Refresh, "Ctrl+R"]
        onActivated: repo.refresh()
    }
    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: openDialog.open()
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6

            ToolButton {
                text: qsTr("Open…")
                onClicked: openDialog.open()
            }
            ToolButton {
                text: qsTr("Refresh")
                onClicked: repo.refresh()
            }
            ToolSeparator {}
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: {
                    if (!repo.valid)
                        return qsTr("No repository")
                    let s = repo.branch.length > 0 ? repo.branch
                                                   : qsTr("detached at %1").arg(repo.headSha.substring(0, 10))
                    if (repo.upstream.length > 0) {
                        s += `  ⇄ ${repo.upstream}`
                        if (repo.ahead > 0)
                            s += `  ↑${repo.ahead}`
                        if (repo.behind > 0)
                            s += `  ↓${repo.behind}`
                    }
                    return s
                }
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        HistoryView {
            repo: repo
            SplitView.preferredHeight: root.height * 0.4
            SplitView.minimumHeight: 100
        }

        SplitView {
            SplitView.fillHeight: true
            orientation: Qt.Horizontal

            ChangesPane {
                repo: repo
                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 220
            }
            DiffView {
                repo: repo
                monoFont: root.monoFont
                SplitView.fillWidth: true
            }
        }
    }

    footer: Pane {
        id: errorBar

        function show(message: string) {
            errorText.text = message
            visible = true
        }

        visible: false
        padding: 6
        background: Rectangle {
            color: Qt.rgba(0.85, 0.2, 0.2, 0.18)
        }

        RowLayout {
            anchors.fill: parent
            Label {
                id: errorText
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
            ToolButton {
                text: "✕"
                onClicked: errorBar.visible = false
            }
        }
    }
}
