pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Igitt
import Igitt.Core
import IgittStyle as Styles

// The repository itself: opening one, cloning one, making a new one; its
// remotes, its worktrees, its submodules; patches saved and applied. A
// menu from the tool bar, and the dialogs it opens.
Item {
    id: tools

    required property Repository repo
    property font monoFont

    // From the lost work: for the window to act on
    signal showCommitRequested(string sha)
    signal branchRequested(string sha, string subject)
    signal resetRequested(string sha, string subject)

    function openMenu(button: Item) {
        menu.popup(button, 0, button.height)
    }
    // At most this many repositories opened before are remembered
    readonly property int recentLimit: 12
    readonly property var recent: AppSettings.repositories.recent ?? []

    function openRepository() {
        openDialog.open()
    }
    function cloneRepository() {
        cloneDialog.ask()
    }
    function newRepository() {
        initDialog.open()
    }
    function remember(given: string) {
        if (given === "")
            return
        // One entry per folder, however it was reached
        const path = repo.canonicalPath(given)
        AppSettings.repositories.recent = [path].concat(recent.filter(p => repo.canonicalPath(p) !== path))
            .slice(0, recentLimit)
    }
    function forget(path: string) {
        AppSettings.repositories.recent = recent.filter(p => p !== path)
    }
    function forgetAll() {
        AppSettings.repositories.recent = []
    }
    // "name  (where it is)"
    function describe(path: string): string {
        const slash = path.lastIndexOf("/")
        return slash < 0 ? path : path.substring(slash + 1) + "  —  " + repo.displayPath(path.substring(0, slash))
    }
    function savePatch(sha: string) {
        patchSaveDialog.sha = sha
        patchSaveDialog.selectedFile = ""
        patchSaveDialog.open()
    }

    Menu {
        id: menu

        objectName: "repositoryMenu"

        MenuItem {
            objectName: "openRepositoryItem"
            text: qsTr("Open…")
            icon.source: Icons.source("folder-open")
            icon.color: palette.windowText
            onTriggered: openDialog.open()
        }
        Menu {
            id: recentMenu
            objectName: "recentMenu"
            title: qsTr("Open Recent")
            enabled: tools.recent.some(path => path !== tools.repo.path)

            Instantiator {
                model: tools.recent.filter(path => path !== tools.repo.path)
                delegate: MenuItem {
                    required property string modelData
                    text: tools.describe(modelData)
                    enabled: tools.repo.folderExists(modelData)
                    onTriggered: tools.repo.open(modelData)
                }
                onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => recentMenu.removeItem(object)
            }
            MenuSeparator {}
            MenuItem {
                objectName: "forgetRecentItem"
                text: qsTr("Clear the List")
                onTriggered: tools.forgetAll()
            }
        }
        MenuItem {
            objectName: "cloneRepositoryItem"
            text: qsTr("Clone…")
            icon.source: Icons.source("copy")
            icon.color: palette.windowText
            onTriggered: cloneDialog.ask()
        }
        MenuItem {
            objectName: "newRepositoryItem"
            text: qsTr("New Repository…")
            icon.source: Icons.source("plus")
            icon.color: palette.windowText
            onTriggered: initDialog.open()
        }
        MenuSeparator {}
        MenuItem {
            objectName: "remotesItem"
            text: qsTr("Remotes…")
            enabled: tools.repo.valid
            icon.source: Icons.source("cloud-upload")
            icon.color: palette.windowText
            onTriggered: remotesDialog.open()
        }
        Menu {
            id: worktreeMenu
            title: qsTr("Worktrees")
            enabled: tools.repo.valid

            MenuItem {
                objectName: "addWorktreeItem"
                text: tools.repo.planning ? qsTr("Add a Worktree (off while planning)") : qsTr("Add a Worktree…")
                enabled: !tools.repo.planning
                icon.source: Icons.source("plus")
                icon.color: palette.windowText
                onTriggered: worktreeDialog.ask()
            }
            MenuSeparator {}
            Instantiator {
                model: tools.repo.worktrees
                delegate: Menu {
                    id: worktree

                    required property var modelData

                    title: (modelData.branch !== "" ? modelData.branch : qsTr("detached")) + "  " + modelData.path
                           + (modelData.current ? qsTr("  (open)") : "")

                    MenuItem {
                        text: qsTr("Open Here")
                        enabled: !worktree.modelData.current
                        onTriggered: tools.repo.open(worktree.modelData.path)
                    }
                    MenuItem {
                        text: qsTr("Remove…")
                        enabled: !worktree.modelData.current && !tools.repo.planning
                        icon.source: Icons.source("trash-2")
                        icon.color: palette.windowText
                        onTriggered: removeWorktreeDialog.ask(worktree.modelData.path)
                    }
                }
                onObjectAdded: (index, object) => worktreeMenu.insertMenu(index + 2, object)
                onObjectRemoved: (index, object) => worktreeMenu.removeMenu(object)
            }
        }
        MenuItem {
            objectName: "updateSubmodulesItem"
            visible: tools.repo.hasSubmodules
            height: visible ? implicitHeight : 0
            text: qsTr("Update Submodules")
            enabled: !tools.repo.planning && !tools.repo.writing
            icon.source: Icons.source("refresh-cw")
            icon.color: palette.windowText
            onTriggered: tools.repo.updateSubmodules()
        }
        Menu {
            id: historyDepthMenu
            objectName: "historyDepthMenu"
            title: qsTr("Fetch More History")
            enabled: tools.repo.valid && tools.repo.shallow && !tools.repo.planning

            MenuItem {
                objectName: "deepenItem"
                text: qsTr("1000 More Commits")
                onTriggered: tools.repo.fetchMoreHistory(1000)
            }
            MenuItem {
                objectName: "unshallowItem"
                text: qsTr("All of It")
                onTriggered: tools.repo.fetchMoreHistory(0)
            }
        }
        MenuItem {
            objectName: "sparseItem"
            text: tools.repo.planning ? qsTr("Sparse Checkout (off while planning)") : qsTr("Sparse Checkout…")
            enabled: tools.repo.valid && !tools.repo.planning
            icon.source: Icons.source("folder-tree")
            icon.color: palette.windowText
            onTriggered: sparseDialog.open()
        }
        MenuItem {
            objectName: "openChangeItem"
            visible: tools.repo.reviewHost === "gerrit"
            height: visible ? implicitHeight : 0
            text: qsTr("Open a Gerrit Change…")
            enabled: tools.repo.valid
            icon.source: Icons.source("git-pull-request")
            icon.color: palette.windowText
            onTriggered: openChangeDialog.ask()
        }
        MenuItem {
            objectName: "recoverItem"
            text: qsTr("Recover Lost Work…")
            enabled: tools.repo.valid
            icon.source: Icons.source("history")
            icon.color: palette.windowText
            onTriggered: recoverDialog.open()
        }
        MenuItem {
            objectName: "applyPatchItem"
            text: tools.repo.planning ? qsTr("Apply a Patch (off while planning)") : qsTr("Apply a Patch…")
            enabled: tools.repo.valid && !tools.repo.planning
            icon.source: Icons.source("git-pull-request")
            icon.color: palette.windowText
            onTriggered: patchOpenDialog.open()
        }
    }

    FolderDialog {
        id: openDialog
        title: qsTr("Open Repository")
        onAccepted: tools.repo.open(selectedFolder)
    }
    FolderDialog {
        id: initDialog
        title: qsTr("New Repository in…")
        onAccepted: tools.repo.initRepository(selectedFolder)
    }
    FolderDialog {
        id: cloneParentDialog
        title: qsTr("Clone into…")
        onAccepted: cloneParent.text = tools.localPath(selectedFolder)
    }
    FolderDialog {
        id: worktreeParentDialog
        title: qsTr("Worktree in…")
        onAccepted: worktreePath.text = tools.localPath(selectedFolder) + "/" + (worktreeBranch.text || "worktree")
    }
    FileDialog {
        id: patchSaveDialog
        property string sha
        title: qsTr("Save the Commit as a Patch")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Patches (*.patch)"), qsTr("All files (*)")]
        onAccepted: tools.repo.savePatch(sha, selectedFile)
    }
    FileDialog {
        id: patchOpenDialog
        title: qsTr("Apply a Patch")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Patches (*.patch *.diff)"), qsTr("All files (*)")]
        onAccepted: tools.repo.applyPatch(selectedFile)
    }

    // Where HEAD was, and commits nothing refers to any more: a branch
    // deleted, a stash dropped, a reset too far. Each can become a branch
    // again, a stash, or where the branch is.
    ModalDialog {
        id: recoverDialog

        property var commits: []
        property bool loading: false
        readonly property var shown: onlyLost.checked ? commits.filter(c => c.unreachable) : commits

        function describe(commit: var): string {
            const when = new Date(commit.time * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
            if (commit.kind === "reflog")
                return `${commit.sha.substring(0, 10)} · ${when} · ${commit.selector}: ${commit.action}`
            return `${commit.sha.substring(0, 10)} · ${when} · `
                + (commit.stash ? qsTr("a dropped stash") : qsTr("nothing refers to it"))
        }

        parent: Overlay.overlay
        objectName: "recoverDialog"
        width: Math.min(720, (parent ? parent.width : 760) - 40)
        title: qsTr("Recover Lost Work")
        standardButtons: Dialog.Close
        onOpened: {
            commits = []
            loading = true
            tools.repo.findLostWork()
        }

        Connections {
            target: tools.repo
            function onLostWorkFound(found: var) {
                recoverDialog.commits = found
                recoverDialog.loading = false
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Where HEAD was, newest first, and the commits nothing refers to any more, "
                           + "such as a deleted branch's or a dropped stash. git keeps them for a while "
                           + "(gc.reflogExpire, gc.pruneExpire).")
            }
            CheckBox {
                id: onlyLost
                objectName: "recoverOnlyLost"
                text: qsTr("Only what no branch, tag or stash has")
                checked: true
            }
            ListView {
                id: lostList
                objectName: "lostWorkList"
                Layout.fillWidth: true
                Layout.preferredHeight: 360
                clip: true
                model: recoverDialog.shown
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    id: lostRow

                    required property var modelData
                    required property int index

                    objectName: "lostWorkRow"
                    width: ListView.view.width
                    rightPadding: 44
                    onClicked: lostMenu.popup()

                    contentItem: ColumnLayout {
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: lostRow.modelData.subject
                            font.weight: Font.Medium
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: recoverDialog.describe(lostRow.modelData)
                            opacity: 0.7
                        }
                    }

                    ToolButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        icon.source: Icons.source("chevron-down")
                        icon.color: palette.windowText
                        onClicked: lostMenu.popup(this, 0, height)
                    }

                    Menu {
                        id: lostMenu

                        MenuItem {
                            objectName: "lostShowItem"
                            text: qsTr("Show")
                            icon.source: Icons.source("git-commit-horizontal")
                            icon.color: palette.windowText
                            onTriggered: {
                                recoverDialog.close()
                                tools.showCommitRequested(lostRow.modelData.sha)
                            }
                        }
                        MenuItem {
                            objectName: "lostBranchItem"
                            text: tools.repo.planning ? qsTr("Make a Branch Here (off while planning)")
                                                      : qsTr("Make a Branch Here…")
                            enabled: !tools.repo.planning
                            icon.source: Icons.source("git-branch")
                            icon.color: palette.windowText
                            onTriggered: {
                                recoverDialog.close()
                                tools.branchRequested(lostRow.modelData.sha, lostRow.modelData.subject)
                            }
                        }
                        MenuItem {
                            objectName: "lostStashItem"
                            visible: lostRow.modelData.stash
                            height: visible ? implicitHeight : 0
                            text: qsTr("Back Among the Stashes")
                            enabled: !tools.repo.planning
                            icon.source: Icons.source("archive")
                            icon.color: palette.windowText
                            onTriggered: {
                                tools.repo.restoreStash(lostRow.modelData.sha, lostRow.modelData.subject)
                                recoverDialog.close()
                            }
                        }
                        MenuItem {
                            objectName: "lostResetItem"
                            text: tools.repo.planning ? qsTr("Reset the Branch Here (off while planning)")
                                                      : qsTr("Reset the Branch Here…")
                            enabled: !tools.repo.planning
                            icon.source: Icons.source("undo-2")
                            icon.color: palette.windowText
                            onTriggered: {
                                recoverDialog.close()
                                tools.resetRequested(lostRow.modelData.sha, lostRow.modelData.subject)
                            }
                        }
                        MenuItem {
                            text: qsTr("Copy SHA")
                            icon.source: Icons.source("copy")
                            icon.color: palette.windowText
                            onTriggered: Clipboard.setText(lostRow.modelData.sha)
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: lostList.count === 0
                    text: recoverDialog.loading ? qsTr("Looking…") : qsTr("Nothing lost.")
                    opacity: 0.7
                }
            }
        }
    }

    function localPath(url: url): string {
        const text = url.toString()
        return text.startsWith("file://") ? decodeURIComponent(text.substring(7)) : text
    }

    // A clone: from where, into which folder, which takes the
    // repository's name
    ModalDialog {
        id: cloneDialog

        readonly property string target: cloneParent.text !== "" && cloneUrl.text.trim() !== ""
                                         ? cloneParent.text + "/" + tools.repo.cloneFolderName(cloneUrl.text) : ""

        function ask() {
            cloneUrl.text = ""
            if (cloneParent.text === "")
                cloneParent.text = tools.repo.valid ? tools.repo.path.substring(0, tools.repo.path.lastIndexOf("/")) : ""
            open()
        }
        function allowOk() {
            const ok = standardButton(Dialog.Ok)
            if (ok)
                ok.enabled = target !== ""
        }

        parent: Overlay.overlay
        objectName: "cloneDialog"
        width: Math.min(560, (parent ? parent.width : 600) - 40)
        title: qsTr("Clone a Repository")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onTargetChanged: allowOk()
        onOpened: {
            allowOk()
            cloneUrl.forceActiveFocus()
        }
        onAccepted: tools.repo.cloneRepository(cloneUrl.text.trim(), cloneParent.text,
                                               cloneShallow.checked ? cloneDepth.value : 0, cloneBlobless.checked)
        Component.onCompleted: {
            standardButton(Dialog.Ok).objectName = "cloneOkButton"
            standardButton(Dialog.Ok).text = qsTr("Clone")
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            TextField {
                id: cloneUrl
                objectName: "cloneUrl"
                Layout.fillWidth: true
                placeholderText: qsTr("URL, e.g. https://github.com/owner/name.git")
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: cloneParent
                    objectName: "cloneParent"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Folder to clone into")
                }
                Button {
                    text: qsTr("Choose…")
                    onClicked: cloneParentDialog.open()
                }
            }
            Label {
                Layout.fillWidth: true
                visible: cloneDialog.target !== ""
                text: qsTr("Into %1").arg(cloneDialog.target)
                elide: Text.ElideMiddle
                color: Styles.TokenInterface.semantics.text_muted
            }
            // For a big repository: less history, or file contents later
            RowLayout {
                CheckBox {
                    id: cloneShallow
                    objectName: "cloneShallow"
                    text: qsTr("Only the last")
                }
                SpinBox {
                    id: cloneDepth
                    objectName: "cloneDepth"
                    enabled: cloneShallow.checked
                    from: 1
                    to: 100000
                    value: 100
                    editable: true
                }
                Label {
                    text: qsTr("commits (--depth)")
                }
            }
            CheckBox {
                id: cloneBlobless
                objectName: "cloneBlobless"
                text: qsTr("File contents only when needed (--filter=blob:none)")
            }
        }
    }

    // A Gerrit change by its number, Change-Id or URL, to review: shown in
    // History with its patch sets, whether it is in the history or not
    ModalDialog {
        id: openChangeDialog

        objectName: "openChangeDialog"

        function ask() {
            changeReference.text = ""
            standardButton(Dialog.Ok).enabled = false
            open()
        }

        parent: Overlay.overlay
        width: Math.min(520, (parent ? parent.width : 560) - 40)
        title: qsTr("Open a Gerrit Change")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: changeReference.forceActiveFocus()
        onAccepted: tools.repo.openChange(changeReference.text)
        Component.onCompleted: {
            standardButton(Dialog.Ok).objectName = "openChangeOkButton"
            standardButton(Dialog.Ok).text = qsTr("Open")
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            TextField {
                id: changeReference
                objectName: "changeReference"
                Layout.fillWidth: true
                placeholderText: qsTr("12345, its URL, or its Change-Id")
                onTextChanged: openChangeDialog.standardButton(Dialog.Ok).enabled = text.trim() !== ""
                onAccepted: {
                    if (text.trim() !== "")
                        openChangeDialog.accept()
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: qsTr("Its current patch set is fetched without keeping a ref, and shown in History with its "
                           + "comments and patch sets to compare.")
            }
        }
    }

    // Sparse checkout: only some folders in the working tree, one a line,
    // the files at the top always
    ModalDialog {
        id: sparseDialog

        objectName: "sparseDialog"
        parent: Overlay.overlay
        width: Math.min(520, (parent ? parent.width : 560) - 40)
        title: qsTr("Sparse Checkout")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: {
            sparseFolders.text = ""
            tools.repo.loadSparseFolders()
        }
        onAccepted: tools.repo.setSparseFolders(sparseFolders.text.split("\n"))
        Component.onCompleted: {
            standardButton(Dialog.Ok).objectName = "sparseOkButton"
            standardButton(Dialog.Ok).text = qsTr("Check Out These")
        }

        Connections {
            target: tools.repo
            function onSparseFoldersLoaded(folders: var) {
                sparseFolders.text = folders.join("\n")
            }
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Only these folders are checked out, besides the files at the top; the rest of "
                           + "the repository stays in git, out of the working tree.")
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                TextArea {
                    id: sparseFolders
                    objectName: "sparseFolders"
                    placeholderText: qsTr("src/app\ndocs")
                    font: tools.monoFont
                }
            }
            Button {
                objectName: "sparseDisableButton"
                visible: tools.repo.sparse
                text: qsTr("Check Out Everything Again")
                onClicked: {
                    tools.repo.disableSparseCheckout()
                    sparseDialog.close()
                }
            }
        }
    }

    // The remotes: each with where it fetches from, to rename, point
    // elsewhere or remove; and a new one
    ModalDialog {
        id: remotesDialog

        parent: Overlay.overlay
        objectName: "remotesDialog"
        width: Math.min(620, (parent ? parent.width : 640) - 40)
        title: qsTr("Remotes")
        standardButtons: Dialog.Close

        ColumnLayout {
            width: parent.width
            spacing: 6

            Repeater {
                model: tools.repo.remotes
                delegate: RowLayout {
                    id: remoteRow

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: remoteRow.modelData.name
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: remoteRow.modelData.url
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: Styles.TokenInterface.semantics.text_muted
                    }
                    Button {
                        text: qsTr("Edit…")
                        enabled: !tools.repo.planning
                        onClicked: remoteDialog.edit(remoteRow.modelData.name, remoteRow.modelData.url)
                    }
                    Button {
                        objectName: "removeRemoteButton"
                        text: qsTr("Remove…")
                        enabled: !tools.repo.planning
                        onClicked: removeRemoteDialog.ask(remoteRow.modelData.name)
                    }
                }
            }
            Label {
                visible: tools.repo.remotes.length === 0
                text: qsTr("No remotes yet.")
                color: Styles.TokenInterface.semantics.text_muted
            }
            Button {
                objectName: "addRemoteButton"
                text: qsTr("Add a Remote…")
                enabled: !tools.repo.planning
                onClicked: remoteDialog.add()
            }
        }
    }

    // A remote's name and where it fetches from: a new one, or one changed
    ModalDialog {
        id: remoteDialog

        property string oldName
        property string oldUrl

        function add() {
            oldName = ""
            oldUrl = ""
            remoteName.text = tools.repo.remotes.length === 0 ? "origin" : ""
            remoteUrl.text = ""
            open()
        }
        function edit(name: string, url: string) {
            oldName = name
            oldUrl = url
            remoteName.text = name
            remoteUrl.text = url
            open()
        }

        parent: Overlay.overlay
        objectName: "remoteDialog"
        width: Math.min(520, (parent ? parent.width : 560) - 40)
        title: oldName === "" ? qsTr("New Remote") : qsTr("Remote %1").arg(oldName)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: remoteName.forceActiveFocus()
        onAccepted: {
            const name = remoteName.text.trim()
            const url = remoteUrl.text.trim()
            if (oldName === "") {
                tools.repo.addRemote(name, url)
                return
            }
            if (name !== oldName)
                tools.repo.renameRemote(oldName, name)
            if (url !== oldUrl)
                tools.repo.setRemoteUrl(name, url)
        }
        Component.onCompleted: standardButton(Dialog.Ok).objectName = "remoteOkButton"

        ColumnLayout {
            width: parent.width
            spacing: 8

            TextField {
                id: remoteName
                objectName: "remoteName"
                Layout.fillWidth: true
                placeholderText: qsTr("Name, e.g. origin")
            }
            TextField {
                id: remoteUrl
                objectName: "remoteUrl"
                Layout.fillWidth: true
                placeholderText: qsTr("URL")
            }
        }
    }

    // Removing a remote takes its remote-tracking branches along
    ModalDialog {
        id: removeRemoteDialog

        property string name

        function ask(remote: string) {
            name = remote
            open()
        }

        parent: Overlay.overlay
        objectName: "removeRemoteDialog"
        width: Math.min(460, (parent ? parent.width : 500) - 40)
        title: qsTr("Remove the remote %1?").arg(name)
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: tools.repo.removeRemote(name)
        Component.onCompleted: {
            const ok = standardButton(Dialog.Ok)
            ok.objectName = "removeRemoteOkButton"
            ok.text = qsTr("Remove")
            const styled = ok as Styles.Button
            if (styled)
                styled.styles = [Styles.ButtonStyle.destructive]
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("Its remote-tracking branches go with it, and branches lose it as their upstream. "
                       + "The remote itself is not touched.")
        }
    }

    // Another work tree of the repository: where, and on which branch,
    // a new one or one there is
    ModalDialog {
        id: worktreeDialog

        function ask() {
            worktreeBranch.text = ""
            worktreeCreate.checked = true
            const here = tools.repo.path
            worktreePath.text = here.substring(0, here.lastIndexOf("/")) + "/"
            open()
        }

        parent: Overlay.overlay
        objectName: "worktreeDialog"
        width: Math.min(560, (parent ? parent.width : 600) - 40)
        title: qsTr("Add a Worktree")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: worktreeBranch.forceActiveFocus()
        onAccepted: tools.repo.addWorktree(worktreePath.text.trim(), worktreeBranch.text.trim(),
                                           worktreeCreate.checked, "")
        Component.onCompleted: standardButton(Dialog.Ok).objectName = "worktreeOkButton"

        ColumnLayout {
            width: parent.width
            spacing: 8

            TextField {
                id: worktreeBranch
                objectName: "worktreeBranch"
                Layout.fillWidth: true
                placeholderText: qsTr("Branch")
            }
            CheckBox {
                id: worktreeCreate
                objectName: "worktreeCreate"
                text: qsTr("A new branch, at HEAD")
                checked: true
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: worktreePath
                    objectName: "worktreePath"
                    Layout.fillWidth: true
                    placeholderText: qsTr("Folder")
                }
                Button {
                    text: qsTr("Choose…")
                    onClicked: worktreeParentDialog.open()
                }
            }
        }
    }

    // Removing a worktree removes its folder; one with changes is refused
    ModalDialog {
        id: removeWorktreeDialog

        property string path

        function ask(folder: string) {
            path = folder
            open()
        }

        parent: Overlay.overlay
        objectName: "removeWorktreeDialog"
        width: Math.min(460, (parent ? parent.width : 500) - 40)
        title: qsTr("Remove the worktree?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: tools.repo.removeWorktree(path)
        Component.onCompleted: {
            const ok = standardButton(Dialog.Ok)
            ok.objectName = "removeWorktreeOkButton"
            ok.text = qsTr("Remove")
            const styled = ok as Styles.Button
            if (styled)
                styled.styles = [Styles.ButtonStyle.destructive]
        }

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: qsTr("%1 is deleted. Its branch stays; git refuses if it has uncommitted changes.")
                  .arg(removeWorktreeDialog.path)
        }
    }
}
