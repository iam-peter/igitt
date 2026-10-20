pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Igitt.Core

ApplicationWindow {
    id: root

    required property string initialPath
    readonly property font uiFont: Qt.font({family: AppSettings.fonts.uiFamily,
                                            pointSize: AppSettings.fonts.uiSize})
    readonly property font monoFont: Qt.font({family: AppSettings.fonts.codeFamily,
                                              pointSize: AppSettings.fonts.codeSize})

    width: 1400
    height: 900
    visible: true
    title: repo.valid ? `${repo.path} — igitt` : "igitt"

    Repository {
        id: repo
        showRemotes: AppSettings.graph.showRemotes
        showTags: AppSettings.graph.showTags
        currentBranchOnly: AppSettings.graph.currentBranchOnly
        ignoreWhitespace: AppSettings.diff.ignoreWhitespace
        pushMode: AppSettings.remote.pushMode
        gerritBranch: AppSettings.remote.gerritBranch
        githubHosts: AppSettings.remote.githubHosts
        gitlabHosts: AppSettings.remote.gitlabHosts
        onPushRejected: (reason, target) => {
            forcePushDialog.reason = reason
            forcePushDialog.target = target
            forcePushDialog.open()
        }
        onErrorOccurred: message => messageBar.show(message, true, [])
        onNotice: (message, links) => messageBar.show(message, false, links)
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

    // Open time-lapse windows. They have no parent (so they are not kept
    // above this window), which leaves them to the garbage collector unless
    // something refers to them.
    property list<Window> timelapses

    function openTimelapse(path: string) {
        timelapses.push(timelapseWindow.createObject(null, {workDir: repo.path, path: path,
                                                           monoFont: root.monoFont, font: root.uiFont}))
    }

    Component {
        id: timelapseWindow
        TimelapseWindow {
            id: timelapse
            onClosing: {
                root.timelapses = Array.from(root.timelapses).filter(w => w !== timelapse)
                timelapse.destroy()
            }
        }
    }

    onClosing: {
        for (const timelapse of Array.from(timelapses))
            timelapse.close()
    }

    function openSettings() {
        settingsDialog.open()
    }
    function closeSettings() {
        settingsDialog.close()
    }
    function showChanges() {
        leftTabs.currentIndex = 0
    }

    function setPlanning(on: bool) {
        if (on)
            repo.startPlanning()
        else if (repo.plan.count === 0)
            repo.discardPlan()
        else
            discardDialog.open()
    }

    // "3 hours ago" for the Fetch button.
    function ago(when: date): string {
        if (isNaN(when.getTime()))
            return qsTr("never")
        const minutes = Math.floor((Date.now() - when.getTime()) / 60000)
        if (minutes < 1)
            return qsTr("just now")
        if (minutes < 60)
            return qsTr("%n min ago", "", minutes)
        if (minutes < 60 * 24)
            return qsTr("%n h ago", "", Math.floor(minutes / 60))
        return qsTr("%n day(s) ago", "", Math.floor(minutes / (60 * 24)))
    }

    // Re-evaluates the relative fetch time now and then.
    property int clock: 0
    Timer {
        interval: 60000
        repeat: true
        running: true
        onTriggered: root.clock++
    }

    ModalDialog {
        id: forcePushDialog

        property string reason
        property string target

        width: Math.min(520, root.width - 40)
        title: qsTr("Push rejected")
        standardButtons: Dialog.Cancel

        ColumnLayout {
            width: parent.width
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The remote refused the push (%1): it has commits your branch does not.")
                          .arg(forcePushDialog.reason)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Fetch and rebase or merge to keep them, or force-push to replace them. "
                           + "Force-pushing uses --force-with-lease: it only replaces what you last fetched.")
                opacity: 0.8
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: qsTr("Fetch")
                    onClicked: {
                        repo.fetch()
                        forcePushDialog.close()
                    }
                }
                Button {
                    objectName: "forcePushButton"
                    text: qsTr("Force-push: %1").arg(forcePushDialog.target)
                    onClicked: {
                        repo.push(true)
                        forcePushDialog.close()
                    }
                }
            }
        }
    }

    SettingsDialog {
        id: settingsDialog
        anchors.centerIn: parent
    }

    ModalDialog {
        id: discardDialog

        width: Math.min(420, root.width - 40)
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

    ModalDialog {
        id: publishedDialog

        property string sha

        width: Math.min(480, root.width - 40)
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

    font: uiFont
    // Plain Text items and painted items use the application font.
    onUiFontChanged: Fonts.setApplicationFont(uiFont)
    Component.onCompleted: {
        Fonts.setApplicationFont(uiFont)
        repo.open(root.initialPath)
    }

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

            IconButton {
                iconName: "folder-open"
                text: qsTr("Open…")
                onClicked: openDialog.open()
            }
            IconButton {
                iconName: "refresh-cw"
                text: qsTr("Refresh")
                onClicked: repo.refresh()
            }
            IconButton {
                iconName: "settings"
                text: qsTr("Settings…")
                onClicked: settingsDialog.open()
            }
            IconButton {
                objectName: "planButton"
                iconName: "list-checks"
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
            IconButton {
                objectName: "fetchButton"
                iconName: "cloud-download"
                text: qsTr("Fetch (%1)").arg((root.clock, root.ago(repo.lastFetch)))
                enabled: repo.valid && !repo.writing && !repo.planning
                onClicked: repo.fetch()
                ToolTip.visible: hovered
                ToolTip.text: repo.planning ? qsTr("Off while planning")
                                            : qsTr("Fetch the upstream's remote and prune deleted branches")
            }
            IconButton {
                id: pushButton
                objectName: "pushButton"
                iconName: repo.pushForReview ? "git-pull-request" : "cloud-upload"
                text: (repo.pushForReview ? qsTr("Push for Review") : qsTr("Push"))
                      + (repo.ahead > 0 ? `  ↑${repo.ahead}` : "")
                enabled: repo.valid && repo.canPush && repo.pushNeeded && !repo.writing && !repo.planning
                         && repo.operation === ""
                onClicked: repo.push()
                ToolTip.visible: hovered
                ToolTip.text: repo.planning ? qsTr("Off while planning")
                            : repo.operation !== "" ? qsTr("Finish the %1 first").arg(repo.operation.toLowerCase())
                            : [repo.pushDescription, repo.reviewSummary].filter(s => s !== "").join("\n")
            }
            IconButton {
                id: reviewOptionsButton
                objectName: "reviewOptionsButton"
                iconName: "chevron-down"
                visible: repo.reviewHost !== ""
                enabled: pushButton.enabled
                onClicked: reviewOptions.opened ? reviewOptions.close() : reviewOptions.open()
                ToolTip.visible: hovered && !reviewOptions.opened
                ToolTip.text: repo.reviewHost === "gerrit"
                              ? qsTr("Push for review with a topic, reviewers, hashtags or as work in progress")
                              : repo.reviewHost === "github" ? qsTr("Push and open a pull request")
                              : qsTr("Push with merge request options")

                ReviewOptionsPopup {
                    id: reviewOptions
                    repo: repo
                    y: reviewOptionsButton.height
                }
            }
            ToolSeparator {}
            BusyIndicator {
                implicitWidth: 24
                implicitHeight: 24
                padding: 2
                running: root.showBusy
                visible: running
            }
            Icon {
                name: "git-branch"
                visible: repo.valid
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

            handle: SplitterHandle {}
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

                handle: SplitterHandle {}
                SplitView.fillHeight: true
                orientation: Qt.Horizontal

                // Changes to stage and commit, or every file of the project.
                ColumnLayout {
                    SplitView.preferredWidth: 360
                    SplitView.minimumWidth: 220
                    spacing: 0

                    TabBar {
                        id: leftTabs
                        Layout.fillWidth: true

                        TabButton {
                            text: qsTr("Changes")
                            icon.source: Icons.source("files")
                            icon.color: palette.buttonText
                            icon.width: 16
                            icon.height: 16
                        }
                        TabButton {
                            objectName: "filesTab"
                            text: qsTr("Files")
                            icon.source: Icons.source("folder-tree")
                            icon.color: palette.buttonText
                            icon.width: 16
                            icon.height: 16
                        }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: leftTabs.currentIndex

                        ChangesPane {
                            repo: repo
                            monoFont: root.monoFont
                            onTimelapseRequested: path => root.openTimelapse(path)
                        }
                        FilesPane {
                            repo: repo
                            onTimelapseRequested: path => root.openTimelapse(path)
                        }
                    }
                }
                DiffView {
                    repo: repo
                    monoFont: root.monoFont
                    onFoldDragMoved: pos => history.updateDrop(pos)
                    onFoldDragFinished: pos => history.finishDrop(pos)
                    onFoldRequested: sha => root.requestFold(sha)
                    onTimelapseRequested: path => root.openTimelapse(path)
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

        property list<string> links

        function show(message: string, isError: bool, links: list<string>) {
            messageText.text = message
            messageBar.isError = isError
            messageBar.links = links
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
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    id: messageText
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
                // What the remote linked to, such as Gerrit's changes.
                Repeater {
                    model: messageBar.links
                    delegate: Label {
                        required property string modelData

                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        textFormat: Text.StyledText
                        text: `<a href="${modelData.replace(/"/g, "%22")}">${modelData.replace(/&/g, "&amp;").replace(/</g, "&lt;")}</a>`
                        onLinkActivated: link => Qt.openUrlExternally(link)

                        HoverHandler {
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }
            Button {
                icon.source: Icons.source("undo-2")
                icon.color: palette.buttonText
                icon.width: 16
                icon.height: 16
                text: qsTr("Undo")
                visible: !messageBar.isError && repo.canUndoFold
                onClicked: repo.undoFold()
            }
            IconButton {
                iconName: "x"
                onClicked: messageBar.visible = false
            }
        }
    }
}
