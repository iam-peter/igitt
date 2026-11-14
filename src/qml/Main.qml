pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

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

    // A whole file folded into a commit: shown, all its lines selected,
    // then folded as selected lines are
    property var pendingFold: null
    function foldFile(path: string, staged: bool, sha: string) {
        const source = staged ? Repository.StagedDiff : Repository.UnstagedDiff
        const shown = repo.diffPath === path && repo.diffSource === source
        // Asking for the file says the diff changed at once, with the lines
        // of the one before: the fold waits for the next time, its own
        if (!shown)
            repo.showFile(path, staged)
        pendingFold = {path: path, source: source, sha: sha}
        if (shown)
            foldPending()
    }
    function foldPending() {
        const fold = pendingFold
        if (!fold || repo.diffPath !== fold.path || repo.diffSource !== fold.source)
            return
        pendingFold = null
        repo.diff.clearSelection()
        repo.diff.selectRange(0, repo.diff.rowCount() - 1)
        requestFold(fold.sha)
    }

    function requestFold(sha: string) {
        if (!repo.isPublished(sha)) {
            repo.foldSelection(sha)
            return
        }
        publishedDialog.ask("fold", sha, "", "")
    }

    // A history edit (see Repository.editCommit()), after asking where it
    // needs a confirmation.
    function requestEdit(kind: string, sha: string, to: string, message: string) {
        if (kind === "drop") {
            dropDialog.sha = sha
            dropDialog.subject = repo.commits.subjectAt(repo.commits.rowOf(sha))
            dropDialog.published = repo.editRewritesPublished(kind, sha, to)
            dropDialog.open()
        } else if (repo.editRewritesPublished(kind, sha, to)) {
            publishedDialog.ask(kind, sha, to, message)
        } else {
            repo.editCommit(kind, sha, to, message)
        }
    }

    // A history edit from Compose, which has them
    function editCommit(kind: string, sha: string) {
        requestEdit(kind, sha, "", "")
    }

    function requestReword(sha: string) {
        rewordDialog.sha = sha
        rewordDialog.loaded = false
        rewordEditor.clear()
        repo.requestMessage(sha)
        rewordDialog.open()
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

    property list<Window> composers

    function openCompose() {
        composers.push(composeWindow.createObject(null, {repo: repo, monoFont: root.monoFont, font: root.uiFont,
                                                        setPlanning: root.setPlanning,
                                                        editCommit: root.editCommit,
                                                        rewordCommit: root.requestReword}))
    }

    Component {
        id: composeWindow
        ComposeWindow {
            id: composer
            onClosing: {
                root.composers = Array.from(root.composers).filter(w => w !== composer)
                composer.destroy()
            }
        }
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
        // They work on this window's repository, which goes with it
        for (const composer of Array.from(composers))
            composer.close()
    }

    // The window's three modes, one for each job: exploring the history
    // (as gitk), committing changes (as git gui), and reworking commits not
    // pushed yet, where the two meet (composing).
    enum Mode { History, Changes, Compose }
    property int mode: Main.Mode.History
    // Chosen once the repository is read: Changes when there are any
    property bool modeChosen: false
    // The commit History showed last, to show again on coming back
    property string historySha
    // The file whose history History shows below the graph, instead of a
    // diff; none for the diff
    property string historyFile
    // While the time-lapse follows the graph, it leaves the graph alone
    property bool followingGraph: false
    // The history of that file, once it is loaded
    readonly property Timelapse fileTimelapse: fileLapse.item ? (fileLapse.item as TimelapseView).timelapse : null

    // History's view of a file: its time-lapse below the graph, which
    // shows the commits that changed it
    function showFileHistory(path: string) {
        historyFile = path
        setMode(Main.Mode.History)
    }
    function closeFileHistory() {
        historyFile = ""
        repo.commits.setFocus([])
    }
    // The file's revision as of a commit of the graph: the commit's own, or
    // the newest one below it, for a commit that left the file as it was
    function revisionAt(sha: string): int {
        const lapse = root.fileTimelapse
        if (!lapse)
            return -1
        const revisions = lapse.revisions
        const exact = revisions.findIndex(revision => revision.sha === sha)
        if (exact >= 0)
            return exact
        const row = repo.commits.rowOf(sha)
        if (row < 0)
            return -1
        let best = -1
        let bestRow = Infinity
        for (let i = 0; i < revisions.length; ++i) {
            const revisionRow = repo.commits.rowOf(revisions[i].sha)
            if (revisionRow >= row && revisionRow < bestRow) {
                best = i
                bestRow = revisionRow
            }
        }
        return best
    }
    // The revision on display, selected in the graph
    function followFile() {
        const lapse = root.fileTimelapse
        if (followingGraph || !lapse || lapse.revision < 0 || lapse.revision >= lapse.revisionCount)
            return
        const sha = lapse.revisions[lapse.revision].sha
        if (repo.diffSource !== Repository.CommitDiff || repo.diffCommit !== sha)
            history.select(sha)
    }

    function setMode(m: int) {
        mode = m
        modeChosen = true
    }
    // Each mode shows its own diff: History a commit or a file, Changes the
    // working tree
    onModeChanged: {
        const working = repo.diffSource === Repository.UnstagedDiff || repo.diffSource === Repository.StagedDiff
        if (mode === Main.Mode.Changes && !working && repo.hasWip)
            repo.showWorkingTree()
        else if (mode === Main.Mode.History && working)
            history.select(historySha !== "" ? historySha : repo.headSha)
    }
    Connections {
        target: repo
        function onDiffChanged() {
            root.foldPending()
            if (repo.diffSource === Repository.CommitDiff) {
                root.historySha = repo.diffCommit
                // The file's time-lapse goes along with the graph
                const revision = root.historyFile !== "" ? root.revisionAt(repo.diffCommit) : -1
                if (revision >= 0 && root.fileTimelapse.revision !== revision) {
                    root.followingGraph = true
                    root.fileTimelapse.revision = revision
                    root.followingGraph = false
                }
            }
            // The uncommitted changes, picked in History, are Changes' to show
            else if (root.mode === Main.Mode.History
                     && (repo.diffSource === Repository.UnstagedDiff || repo.diffSource === Repository.StagedDiff))
                root.setMode(Main.Mode.Changes)
        }
        function onHeadChanged() {
            if (!root.modeChosen && repo.valid) {
                root.modeChosen = true
                root.mode = repo.hasWip ? Main.Mode.Changes : Main.Mode.History
            }
        }
        function onPathChanged() {
            root.modeChosen = false
            root.historySha = ""
            root.closeFileHistory()
        }
    }
    // Compose, with the commit's column first
    function editInCompose(sha: string) {
        setMode(Main.Mode.Compose)
        Qt.callLater(() => {
            if (compose.item)
                compose.item.revealCommit(sha)
        })
    }
    function showCommitInHistory(sha: string) {
        setMode(Main.Mode.History)
        history.select(sha)
    }

    function openSettings() {
        settingsDialog.open()
    }
    function closeSettings() {
        settingsDialog.close()
    }
    function showChanges() {
        setMode(Main.Mode.Changes)
    }
    // A stash to drop, after asking
    function askDropStash(index: int) {
        dropStashDialog.ask(index)
    }
    function showFiles() {
        setMode(Main.Mode.History)
    }
    function showHistory() {
        setMode(Main.Mode.History)
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
        objectName: "settingsDialog"
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

        property string kind
        property string sha
        property string to
        property string message

        function ask(kind: string, sha: string, to: string, message: string) {
            this.kind = kind
            this.sha = sha
            this.to = to
            this.message = message
            open()
        }

        objectName: "publishedDialog"
        width: Math.min(480, root.width - 40)
        title: qsTr("Rewrite a published commit?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: {
            if (kind === "fold")
                repo.foldSelection(sha)
            else
                repo.editCommit(kind, sha, to, message)
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: publishedDialog.kind === "fold"
                  ? qsTr("This commit is already on a remote branch. Folding into it rewrites it and "
                         + "every commit after it, so you will have to force push.")
                  : qsTr("This rewrites commits that are already on a remote branch, so you will have "
                         + "to force push.")
        }
    }

    // Throwing changes away cannot be undone: asked first, with a red
    // button. A file's unstaged changes, or the lines selected.
    ModalDialog {
        id: discardChangesDialog

        property string path
        property int lines

        function askFile(file: string) {
            path = file
            lines = 0
            open()
        }
        function askLines(count: int) {
            path = ""
            lines = count
            open()
        }

        objectName: "discardChangesDialog"
        width: Math.min(460, root.width - 40)
        title: path !== "" ? qsTr("Discard the changes to %1?").arg(path)
                           : qsTr("Discard %n line(s)?", "", lines)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: path !== "" ? repo.discardFile(path) : repo.discardSelection()
        Component.onCompleted: {
            const ok = standardButton(Dialog.Ok)
            ok.objectName = "discardOkButton"
            ok.text = qsTr("Discard")
            const styled = ok as Styles.Button
            if (styled)
                styled.styles = [Styles.ButtonStyle.destructive]
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("This cannot be undone: the changes are gone from the working tree, and not kept "
                       + "anywhere. Staged changes stay; an untracked file is deleted.")
        }
    }

    // Stashing the changes: a message, and whether untracked files go too
    ModalDialog {
        id: stashDialog

        objectName: "stashDialog"
        width: Math.min(460, root.width - 40)
        title: qsTr("Stash the changes")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            stashMessage.text = ""
            stashMessage.forceActiveFocus()
        }
        onAccepted: repo.stash(stashMessage.text, stashUntracked.checked)
        Component.onCompleted: {
            standardButton(Dialog.Ok).objectName = "stashOkButton"
            standardButton(Dialog.Ok).text = qsTr("Stash")
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            TextField {
                id: stashMessage
                objectName: "stashMessage"
                Layout.fillWidth: true
                placeholderText: qsTr("What they are, for later (optional)")
                onAccepted: stashDialog.accept()
            }
            CheckBox {
                id: stashUntracked
                objectName: "stashUntracked"
                text: qsTr("Untracked files too")
                checked: true
            }
        }
    }

    // The current branch reset to a commit: what lies between kept staged,
    // unstaged, or with every uncommitted change thrown away, which is
    // red and says so
    ModalDialog {
        id: resetDialog

        property string sha
        property string subject
        readonly property string mode: resetSoft.checked ? "soft" : resetHard.checked ? "hard" : "mixed"

        function ask(commit: string, text: string) {
            sha = commit
            subject = text
            resetMixed.checked = true
            open()
        }
        function styleOk() {
            const ok = standardButton(Dialog.Ok)
            const styled = ok as Styles.Button
            if (styled)
                styled.styles = mode === "hard" ? [Styles.ButtonStyle.destructive] : [Styles.ButtonStyle.primary]
        }

        objectName: "resetDialog"
        width: Math.min(500, root.width - 40)
        title: qsTr("Reset %1 to %2?").arg(repo.branch || "HEAD").arg(sha.substring(0, 10))
        standardButtons: Dialog.Ok | Dialog.Cancel
        onModeChanged: styleOk()
        onOpened: styleOk()
        onAccepted: repo.resetTo(sha, mode)
        Component.onCompleted: {
            standardButton(Dialog.Ok).objectName = "resetOkButton"
            standardButton(Dialog.Ok).text = qsTr("Reset")
        }

        ColumnLayout {
            width: parent.width
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: resetDialog.subject
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Styles.TokenInterface.semantics.text_muted
            }
            RadioButton {
                id: resetSoft
                objectName: "resetSoft"
                text: qsTr("Keep what the commits after it changed, staged")
            }
            RadioButton {
                id: resetMixed
                objectName: "resetMixed"
                text: qsTr("Keep it, unstaged")
                checked: true
            }
            RadioButton {
                id: resetHard
                objectName: "resetHard"
                text: qsTr("Throw it away, and every uncommitted change")
            }
            Label {
                Layout.fillWidth: true
                visible: resetHard.checked
                wrapMode: Text.Wrap
                text: qsTr("This cannot be undone here: the uncommitted changes are lost for good, and the "
                           + "commits after it can only be found again in the reflog.")
                color: Styles.TokenInterface.semantics.danger_default
            }
        }
    }

    // Dropping a stash cannot be undone either
    ModalDialog {
        id: dropStashDialog

        property int index

        function ask(at: int) {
            index = at
            open()
        }

        objectName: "dropStashDialog"
        width: Math.min(460, root.width - 40)
        title: qsTr("Drop stash@{%1}?").arg(index)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: repo.dropStash(index)
        Component.onCompleted: {
            const ok = standardButton(Dialog.Ok)
            ok.objectName = "dropStashOkButton"
            ok.text = qsTr("Drop Stash")
            const styled = ok as Styles.Button
            if (styled)
                styled.styles = [Styles.ButtonStyle.destructive]
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("“%1” is thrown away, and cannot be brought back here.")
                  .arg(repo.stashes[dropStashDialog.index] ?? "")
        }
    }

    BranchDialog {
        id: branchDialog
        repo: repo
    }

    // Deleting a branch cannot be undone here, so it is guarded: the
    // branch's name typed again, before the button deletes it
    ModalDialog {
        id: deleteBranchDialog

        property string name
        readonly property bool confirmed: confirmField.text === name

        function ask(branch: string) {
            name = branch
            force.checked = false
            confirmField.text = ""
            open()
        }
        function allowDelete() {
            const ok = standardButton(Dialog.Ok)
            if (ok)
                ok.enabled = confirmed
        }

        objectName: "deleteBranchDialog"
        width: Math.min(460, root.width - 40)
        title: qsTr("Delete the branch %1?").arg(name)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onConfirmedChanged: allowDelete()
        onOpened: {
            allowDelete()
            confirmField.forceActiveFocus()
        }
        onAccepted: {
            if (confirmed)
                repo.deleteBranch(name, force.checked)
        }
        Component.onCompleted: {
            const ok = standardButton(Dialog.Ok)
            ok.objectName = "deleteBranchOkButton"
            ok.text = qsTr("Delete Branch")
            const styled = ok as Styles.Button
            if (styled)
                styled.styles = [Styles.ButtonStyle.destructive]
        }

        ColumnLayout {
            width: parent.width
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("This cannot be undone here. Its commits stay where other branches have them; "
                           + "one not merged anywhere else is only deleted when that is ticked below, and "
                           + "then can only be found again in the reflog.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Type %1 to confirm.").arg(deleteBranchDialog.name)
                font.bold: true
            }
            TextField {
                id: confirmField
                objectName: "deleteBranchConfirm"
                Layout.fillWidth: true
                placeholderText: deleteBranchDialog.name
                onAccepted: {
                    if (deleteBranchDialog.confirmed)
                        deleteBranchDialog.accept()
                }
            }
            CheckBox {
                id: force
                objectName: "deleteBranchForce"
                text: qsTr("Even if it is not merged")
            }
        }
    }

    ModalDialog {
        id: dropDialog

        property string sha
        property string subject
        property bool published

        objectName: "dropDialog"
        width: Math.min(480, root.width - 40)
        title: qsTr("Drop “%1”?").arg(subject)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: repo.editCommit("drop", sha, "", "")
        Component.onCompleted: standardButton(Dialog.Ok).objectName = "dropOkButton"

        ColumnLayout {
            width: parent.width
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The commit is removed from the branch and the commits after it are rewritten. "
                           + "The working tree is not touched: what the commit changed shows up as "
                           + "uncommitted changes, to keep or discard.")
            }
            Label {
                Layout.fillWidth: true
                visible: dropDialog.published
                wrapMode: Text.Wrap
                text: qsTr("The commit is already on a remote branch, so you will have to force push.")
                color: Styles.TokenInterface.semantics.danger_default
            }
        }
    }

    ModalDialog {
        id: rewordDialog

        property string sha
        property bool loaded

        objectName: "rewordDialog"
        width: Math.min(640, root.width - 40)
        height: Math.min(460, root.height - 40)
        title: qsTr("Reword %1").arg(sha.slice(0, 10))
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.requestEdit("reword", sha, "", rewordEditor.message())
        onOpened: rewordEditor.focusTitle()
        Component.onCompleted: standardButton(Dialog.Ok).objectName = "rewordOkButton"

        Binding {
            target: rewordDialog.standardButton(Dialog.Ok)
            property: "enabled"
            value: rewordDialog.loaded && rewordEditor.titleAccepted
        }
        Connections {
            target: repo
            function onMessageLoaded(sha: string, message: string) {
                if (sha !== rewordDialog.sha || rewordDialog.loaded)
                    return
                rewordEditor.load(message)
                rewordDialog.loaded = true
            }
        }
        Shortcut {
            sequences: ["Ctrl+Return", "Ctrl+Enter"]
            enabled: rewordDialog.visible && rewordEditor.editing && rewordDialog.loaded
                     && rewordEditor.titleAccepted
            onActivated: rewordDialog.accept()
        }

        MessageEditor {
            id: rewordEditor
            anchors.fill: parent
            namePrefix: "reword"
            monoFont: root.monoFont
            enabled: rewordDialog.loaded
        }
    }

    font: uiFont
    // The graph's lanes in the theme's colors, for the models and the
    // painted items.
    Binding {
        target: LaneColors
        property: "colors"
        value: Styles.TokenInterface.semantics.graph_lanes.map(color => color.toString())
    }
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

    // Not while a dialog asks something about the repository as it is, or
    // a menu or popup is open.
    Shortcut {
        sequences: [StandardKey.Refresh, "Ctrl+R"]
        enabled: !root.popupOpen
        onActivated: repo.refresh()
    }
    Shortcut {
        sequence: "Ctrl+1"
        enabled: !root.popupOpen
        onActivated: root.setMode(Main.Mode.History)
    }
    Shortcut {
        sequence: "Ctrl+2"
        enabled: !root.popupOpen
        onActivated: root.setMode(Main.Mode.Changes)
    }
    Shortcut {
        sequence: "Ctrl+3"
        enabled: !root.popupOpen && repo.valid
        onActivated: root.setMode(Main.Mode.Compose)
    }
    Shortcut {
        sequences: [StandardKey.Open]
        enabled: !root.popupOpen
        onActivated: openDialog.open()
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6

            // The modes, side by side, the current one checked
            component ModeButton: IconButton {
                required property int target
                property string shortcut

                styles: [Styles.ButtonStyle.medium]
                checkable: true
                checked: root.mode === target
                onClicked: root.setMode(target)
                ToolTip.visible: hovered
                ToolTip.delay: 500
            }
            ModeButton {
                objectName: "historyModeButton"
                target: Main.Mode.History
                iconName: "history"
                text: qsTr("History")
                ToolTip.text: qsTr("Explore the history and the files (Ctrl+1)")
            }
            ModeButton {
                objectName: "changesModeButton"
                readonly property int count: (repo.wipCounts.modified ?? 0) + (repo.wipCounts.added ?? 0)
                                             + (repo.wipCounts.deleted ?? 0)
                target: Main.Mode.Changes
                iconName: "files"
                text: count > 0 ? qsTr("Changes  %1").arg(count) : qsTr("Changes")
                ToolTip.text: qsTr("Stage and commit the changes (Ctrl+2)")
            }
            ModeButton {
                objectName: "composeButton"
                target: Main.Mode.Compose
                enabled: repo.valid
                iconName: "boxes"
                text: repo.ahead > 0 ? qsTr("Compose  ↑%1").arg(repo.ahead) : qsTr("Compose")
                ToolTip.text: qsTr("Group the changes into commits, and rework the commits not pushed yet (Ctrl+3)")
            }
            ToolSeparator {}
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                // Icons only, to leave the tool bar room
                iconName: "folder-open"
                Accessible.name: qsTr("Open…")
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Open… (Ctrl+O)")
                onClicked: openDialog.open()
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                // Icons only, to leave the tool bar room
                iconName: "refresh-cw"
                Accessible.name: qsTr("Refresh")
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Refresh (F5)")
                onClicked: repo.refresh()
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
                // Icons only, to leave the tool bar room
                iconName: "settings"
                Accessible.name: qsTr("Settings…")
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Settings…")
                onClicked: settingsDialog.open()
            }
            IconButton {
                styles: [Styles.ButtonStyle.medium]
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
            // The stashes: the changes put away, and those put away before
            IconButton {
                id: stashButton
                styles: [Styles.ButtonStyle.medium]
                objectName: "stashButton"
                iconName: "archive"
                text: repo.stashes.length > 0 ? `${repo.stashes.length}` : ""
                Accessible.name: qsTr("Stashes")
                enabled: repo.valid
                onClicked: stashMenu.popup(stashButton, 0, stashButton.height)
                ToolTip.visible: hovered && !stashMenu.visible
                ToolTip.delay: 300
                ToolTip.text: qsTr("Stash the changes, or apply one stashed before (%n stashed)", "", repo.stashes.length)

                Menu {
                    id: stashMenu

                    MenuItem {
                        objectName: "stashChangesItem"
                        text: repo.planning ? qsTr("Stash Changes (off while planning)") : qsTr("Stash Changes…")
                        enabled: repo.hasWip && !repo.planning && !repo.writing
                        icon.source: Icons.source("archive")
                        icon.color: palette.windowText
                        onTriggered: stashDialog.open()
                    }
                    MenuSeparator {
                        visible: repo.stashes.length > 0
                    }
                    Instantiator {
                        model: repo.stashes
                        delegate: Menu {
                            id: entry

                            required property string modelData
                            required property int index

                            title: `stash@{${index}}  ${modelData}`

                            MenuItem {
                                objectName: "applyStashItem"
                                text: qsTr("Apply, and keep it")
                                enabled: !repo.planning && !repo.writing
                                onTriggered: repo.applyStash(entry.index, false)
                            }
                            MenuItem {
                                objectName: "popStashItem"
                                text: qsTr("Pop: apply, and drop it")
                                enabled: !repo.planning && !repo.writing
                                onTriggered: repo.applyStash(entry.index, true)
                            }
                            MenuItem {
                                objectName: "dropStashItem"
                                text: qsTr("Drop…")
                                enabled: !repo.planning && !repo.writing
                                icon.source: Icons.source("trash-2")
                                icon.color: palette.windowText
                                onTriggered: dropStashDialog.ask(entry.index)
                            }
                        }
                        onObjectAdded: (index, object) => stashMenu.insertMenu(index + 2, object)
                        onObjectRemoved: (index, object) => stashMenu.removeMenu(object)
                    }
                }
            }
            ToolSeparator {}
            IconButton {
                styles: [Styles.ButtonStyle.medium]
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
                styles: [Styles.ButtonStyle.medium]
                objectName: "pullButton"
                iconName: "arrow-down"
                text: qsTr("Pull") + (repo.behind > 0 ? `  ↓${repo.behind}` : "")
                enabled: repo.valid && repo.upstream !== "" && !repo.writing && !repo.planning && repo.operation === ""
                onClicked: repo.pull()
                ToolTip.visible: hovered
                ToolTip.text: repo.planning ? qsTr("Off while planning")
                            : repo.operation !== "" ? qsTr("Finish the %1 first").arg(repo.operation.toLowerCase())
                            : repo.upstream === "" ? qsTr("The branch has no upstream to pull from")
                            : qsTr("Fetch %1 and merge it, or rebase onto it as pull.rebase says").arg(repo.upstream)
            }
            IconButton {
                id: pushButton
                styles: [Styles.ButtonStyle.medium]
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
                styles: [Styles.ButtonStyle.medium]
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
            // The current branch, and the branches to switch to
            ToolButton {
                id: branchButton
                objectName: "branchButton"
                // As wide as the branch and its upstream: without text, the
                // style takes a button for one with an icon only, square
                implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                // Branches cannot be planned: off while planning, and says so
                enabled: repo.valid && !repo.planning
                onClicked: switcher.opened ? switcher.close() : switcher.open()
                ToolTip.visible: hovered && !switcher.opened
                ToolTip.delay: repo.planning ? 0 : 500
                ToolTip.text: repo.planning ? qsTr("Off while planning: branches cannot be planned")
                                            : qsTr("Switch to another branch, or make one")

                contentItem: RowLayout {
                    spacing: 6

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
                    Icon {
                        name: "chevron-down"
                        visible: repo.valid
                        opacity: 0.6
                    }
                }

                BranchSwitcher {
                    id: switcher
                    repo: repo
                    y: branchButton.height
                    onNewBranchRequested: branchDialog.create(repo.headSha, repo.headMessage)
                    onRenameRequested: name => branchDialog.rename(name)
                    onDeleteRequested: name => deleteBranchDialog.ask(name)
                }
            }
            Item {
                Layout.fillWidth: true
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
            color: Qt.alpha(Styles.TokenInterface.semantics.special_default, 0.2)

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

        // A merge, rebase, cherry-pick or revert stopped at conflicts: they
        // are resolved in the changes, then it goes on, or it is undone
        Pane {
            objectName: "operationBanner"
            Layout.fillWidth: true
            visible: repo.operation !== ""
            padding: 6
            background: Rectangle {
                color: Qt.alpha(Styles.TokenInterface.semantics.warning_default, 0.2)
            }

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("%1: resolve the conflicts in Changes and stage them, then continue; "
                               + "or abort, which puts everything back as it was.").arg(repo.operation)
                }
                IconButton {
                    objectName: "continueOperationButton"
                    iconName: "play"
                    text: qsTr("Continue")
                    enabled: !repo.writing
                    onClicked: repo.continueOperation()
                }
                IconButton {
                    objectName: "abortOperationButton"
                    iconName: "undo-2"
                    text: qsTr("Abort")
                    enabled: !repo.writing
                    onClicked: repo.abortOperation()
                }
            }
        }

        SplitView {
            handle: SplitterHandle {}
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            StackLayout {
                id: modes
                SplitView.fillWidth: true
                currentIndex: root.mode

                // History: the graph above, a commit's or a file's diff
                // below, beside every file of the project
                SplitView {
                    handle: SplitterHandle {}
                    orientation: Qt.Vertical

                    HistoryView {
                        id: history
                        repo: repo
                        onFoldRequested: sha => root.requestFold(sha)
                        onComposeRequested: sha => root.editInCompose(sha)
                        onBranchRequested: (sha, subject) => branchDialog.create(sha, subject)
                        onCheckoutRequested: name => repo.checkoutBranch(name)
                        onRenameBranchRequested: name => branchDialog.rename(name)
                        onDeleteBranchRequested: name => deleteBranchDialog.ask(name)
                        onMergeRequested: name => repo.mergeBranch(name)
                        onCherryPickRequested: sha => repo.cherryPick(sha)
                        onRevertRequested: sha => repo.revertCommit(sha)
                        onResetRequested: (sha, subject) => resetDialog.ask(sha, subject)
                        onRebaseRequested: name => repo.rebaseOnto(name)
                        SplitView.preferredHeight: root.height * 0.45
                        SplitView.minimumHeight: 100
                    }
                    SplitView {
                        handle: SplitterHandle {}
                        SplitView.fillHeight: true
                        orientation: Qt.Horizontal

                        FilesPane {
                            repo: repo
                            currentFile: root.historyFile
                            SplitView.preferredWidth: 300
                            SplitView.minimumWidth: 200
                            onTimelapseRequested: path => root.showFileHistory(path)
                        }
                        Item {
                            id: historyDiff
                            SplitView.fillWidth: true

                            // A file's history instead of the diff, linked
                            // with the graph
                            Loader {
                                id: fileLapse
                                anchors.fill: parent
                                z: 1
                                active: root.historyFile !== ""
                                sourceComponent: TimelapseView {
                                    objectName: "historyTimelapse"
                                    embedded: true
                                    workDir: repo.path
                                    path: root.historyFile
                                    monoFont: root.monoFont
                                    active: root.mode === Main.Mode.History && root.active
                                    onPopOutRequested: {
                                        root.openTimelapse(path)
                                        root.closeFileHistory()
                                    }
                                    onCloseRequested: root.closeFileHistory()
                                }
                            }
                            Connections {
                                target: root.fileTimelapse
                                // The graph shows the commits of the file
                                function onRevisionsChanged() {
                                    repo.commits.setFocus(root.fileTimelapse.revisions.map(revision => revision.sha))
                                }
                                function onRevisionChanged() {
                                    root.followFile()
                                }
                            }
                        }
                    }
                }

                // Changes: the last commits for context, then the changes to
                // stage and commit beside their diff
                ColumnLayout {
                    spacing: 0

                    RecentCommits {
                        id: recent
                        Layout.fillWidth: true
                        repo: repo
                        onCommitClicked: sha => root.showCommitInHistory(sha)
                    }
                    SplitView {
                        handle: SplitterHandle {}
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        orientation: Qt.Horizontal

                        ChangesPane {
                            repo: repo
                            monoFont: root.monoFont
                            onDiscardRequested: path => discardChangesDialog.askFile(path)
                            foldInto: recent.dropSubject
                            foldRefused: recent.dropBlocker
                            onFileDragMoved: (path, staged, pos) => recent.updateDrop(pos, true)
                            onFileDropped: (path, staged, pos) => {
                                const sha = recent.finishDrop(pos, true)
                                if (sha !== "")
                                    root.foldFile(path, staged, sha)
                            }
                            SplitView.preferredWidth: 360
                            SplitView.minimumWidth: 220
                            onTimelapseRequested: path => root.showFileHistory(path)
                        }
                        Item {
                            id: changesDiff
                            SplitView.fillWidth: true
                        }
                    }
                }

                // Compose: commits not pushed yet beside the changes in
                // groups; read only while in sight
                Loader {
                    id: compose
                    active: root.mode === Main.Mode.Compose || item !== null
                    sourceComponent: ComposeView {
                        repo: repo
                        monoFont: root.monoFont
                        setPlanning: root.setPlanning
                        editCommit: root.editCommit
                        rewordCommit: root.requestReword
                        showPlanButtons: false
                        active: root.mode === Main.Mode.Compose && root.active
                    }
                }
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

        // One diff, for History and Changes, in the mode shown
        DiffView {
            parent: root.mode === Main.Mode.Changes ? changesDiff : historyDiff
            anchors.fill: parent
            // Not under a file's history in History
            visible: root.mode === Main.Mode.Changes || root.historyFile === ""
            repo: repo
            monoFont: root.monoFont
            // The selected lines fold into a commit of the strip above
            canDropFold: root.mode === Main.Mode.Changes && recent.visible
            onFoldDragMoved: pos => recent.updateDrop(pos, false)
            onFoldDragFinished: pos => {
                const sha = recent.finishDrop(pos, false)
                if (sha !== "")
                    root.requestFold(sha)
            }
            onFoldRequested: sha => root.requestFold(sha)
            onDiscardLinesRequested: discardChangesDialog.askLines(repo.diff.selectedCount)
            onTimelapseRequested: path => root.showFileHistory(path)
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
            color: Qt.alpha(messageBar.isError ? Styles.TokenInterface.semantics.danger_default : Styles.TokenInterface.semantics.success_default, 0.18)
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
                styles: [Styles.ButtonStyle.small]
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
