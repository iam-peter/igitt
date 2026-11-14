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

    function openMenu(button: Item) {
        menu.popup(button, 0, button.height)
    }
    function openRepository() {
        openDialog.open()
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
        onAccepted: tools.repo.cloneRepository(cloneUrl.text.trim(), cloneParent.text)
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
