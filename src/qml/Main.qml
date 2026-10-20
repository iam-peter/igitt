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
        onErrorOccurred: message => messageBar.show(message, true)
        onNotice: message => messageBar.show(message, false)
    }

    function requestFold(sha: string) {
        const remote = repo.publishedIn(sha)
        if (remote === "") {
            repo.foldSelection(sha)
            return
        }
        publishedDialog.sha = sha
        publishedDialog.remote = remote
        publishedDialog.open()
    }

    Dialog {
        id: publishedDialog

        property string sha
        property string remote

        anchors.centerIn: parent
        width: Math.min(480, root.width - 40)
        modal: true
        title: qsTr("Rewrite a published commit?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: repo.foldSelection(sha)

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("This commit is already on %1. Folding into it rewrites it and every commit "
                       + "after it, so you will have to force push.").arg(publishedDialog.remote)
        }
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
            id: history
            repo: repo
            onFoldRequested: sha => root.requestFold(sha)
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
                onFoldDragMoved: pos => history.updateDrop(pos)
                onFoldDragFinished: pos => history.finishDrop(pos)
                onFoldRequested: sha => root.requestFold(sha)
                SplitView.fillWidth: true
            }
        }
    }

    footer: Pane {
        id: messageBar

        property bool isError

        function show(message: string, isError: bool) {
            messageText.text = message
            messageBar.isError = isError
            visible = true
        }

        objectName: "messageBar"
        visible: false
        padding: 6
        background: Rectangle {
            color: messageBar.isError ? Qt.rgba(0.85, 0.2, 0.2, 0.18) : Qt.rgba(0.2, 0.6, 0.3, 0.16)
        }

        RowLayout {
            anchors.fill: parent
            Label {
                id: messageText
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
            Button {
                text: qsTr("Undo")
                visible: !messageBar.isError && repo.canUndoFold
                onClicked: repo.undoFold()
            }
            ToolButton {
                text: "✕"
                onClicked: messageBar.visible = false
            }
        }
    }
}
