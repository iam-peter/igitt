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
        showRemotes: AppSettings.graph.showRemotes
        showTags: AppSettings.graph.showTags
        currentBranchOnly: AppSettings.graph.currentBranchOnly
        onErrorOccurred: message => messageBar.show(message, true)
        onNotice: message => messageBar.show(message, false)
    }

    function requestFold(sha: string) {
        if (!repo.isPublished(sha)) {
            repo.foldSelection(sha)
            return
        }
        publishedDialog.sha = sha
        publishedDialog.open()
    }

    // Only show the busy indicator for jobs that take a noticeable time.
    property bool showBusy: false
    Timer {
        interval: 300
        running: repo.busy && !root.showBusy
        onTriggered: root.showBusy = true
    }
    Connections {
        target: repo
        function onBusyChanged() {
            if (!repo.busy)
                root.showBusy = false
        }
    }

    function setPlanning(on: bool) {
        if (on)
            repo.startPlanning()
        else if (repo.plan.count === 0)
            repo.discardPlan()
        else
            discardDialog.open()
    }

    SettingsDialog {
        id: settingsDialog
        anchors.centerIn: parent
    }

    Dialog {
        id: discardDialog

        anchors.centerIn: parent
        width: Math.min(420, root.width - 40)
        modal: true
        title: qsTr("Discard the plan?")
        standardButtons: Dialog.Discard | Dialog.Cancel
        onDiscarded: {
            repo.discardPlan()
            close()
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("%n planned action(s) will be dropped. The repository is not affected.", "",
                       repo.plan.count)
        }
    }

    Dialog {
        id: publishedDialog

        property string sha

        anchors.centerIn: parent
        width: Math.min(480, root.width - 40)
        modal: true
        title: qsTr("Rewrite a published commit?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: repo.foldSelection(sha)

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("This commit is already on a remote branch. Folding into it rewrites it and "
                       + "every commit after it, so you will have to force push.")
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
            ToolButton {
                text: qsTr("Settings…")
                onClicked: settingsDialog.open()
            }
            ToolButton {
                objectName: "planButton"
                text: qsTr("Plan")
                checkable: true
                checked: repo.planning
                enabled: repo.valid && !repo.writing
                onClicked: root.setPlanning(checked)
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Collect actions in a plan, review the commands, and execute them together")
            }
            ToolSeparator {}
            BusyIndicator {
                implicitWidth: 24
                implicitHeight: 24
                padding: 2
                running: root.showBusy
                visible: running
            }
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            visible: repo.planning
            implicitHeight: planBanner.implicitHeight + 8
            color: Qt.rgba(0.55, 0.35, 0.85, 0.2)

            Label {
                id: planBanner
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 8
                elide: Text.ElideRight
                text: qsTr("Planning: actions are simulated. History, files and diffs show the result; "
                           + "nothing changes in the repository until you execute the plan.")
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                    monoFont: root.monoFont
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
                PlanPane {
                    visible: repo.planning
                    repo: repo
                    monoFont: root.monoFont
                    SplitView.preferredWidth: 380
                    SplitView.minimumWidth: 240
                    onDiscardRequested: root.setPlanning(false)
                }
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
