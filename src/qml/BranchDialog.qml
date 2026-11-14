pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// A branch's name: for a new one at a commit, switched to at once unless
// unticked, or for one renamed. Says what is wrong with a name before it
// is taken (Repository.branchNameError()).
ModalDialog {
    id: dialog

    required property Repository repo
    // "create" at `start`, or "rename" `oldName`
    property string mode: "create"
    property string start
    property string startLabel
    property string oldName
    readonly property string name: nameField.text.trim()
    readonly property string error: dialog.repo.branchNameError(name)

    function create(sha: string, subject: string) {
        mode = "create"
        start = sha
        startLabel = `${sha.substring(0, 10)}  ${subject}`
        nameField.text = ""
        checkout.checked = true
        open()
    }
    function rename(branch: string) {
        mode = "rename"
        oldName = branch
        nameField.text = branch
        open()
    }

    objectName: "branchDialog"
    width: 460
    title: mode === "create" ? qsTr("New Branch") : qsTr("Rename %1").arg(oldName)
    standardButtons: Dialog.Ok | Dialog.Cancel
    onOpened: {
        allowOk()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }
    onAccepted: {
        if (mode === "create")
            repo.createBranch(name, start, checkout.checked)
        else
            repo.renameBranch(oldName, name)
    }
    Component.onCompleted: standardButton(Dialog.Ok).objectName = "branchOkButton"

    // Not with a name git would refuse; the button is only made with the
    // dialog, after a binding to it would have been set up
    function allowOk() {
        const ok = standardButton(Dialog.Ok)
        if (ok)
            ok.enabled = error === ""
    }
    onErrorChanged: allowOk()

    ColumnLayout {
        width: parent.width
        spacing: 8

        Label {
            Layout.fillWidth: true
            visible: dialog.mode === "create"
            text: qsTr("At %1").arg(dialog.startLabel)
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Styles.TokenInterface.semantics.text_muted
        }
        TextField {
            id: nameField
            objectName: "branchName"
            Layout.fillWidth: true
            placeholderText: qsTr("Name, e.g. fix/login-form")
            onAccepted: {
                if (dialog.error === "")
                    dialog.accept()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.name !== "" && dialog.error !== ""
            text: dialog.error
            wrapMode: Text.Wrap
            color: Styles.TokenInterface.semantics.danger_default
        }
        CheckBox {
            id: checkout
            objectName: "branchCheckout"
            visible: dialog.mode === "create"
            text: qsTr("Switch to it")
            checked: true
        }
    }
}
