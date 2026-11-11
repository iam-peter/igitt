pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Igitt.Core

// The message of a cluster's commit, written in the same editor as any
// commit's: title, body and trailers, with the same limits.
ModalDialog {
    id: dialog

    required property ChangeClusters clusters
    required property font monoFont
    property int cluster: -1

    function edit(id: int) {
        cluster = id
        editor.load(clusters.clusterMessage(id))
        open()
    }

    width: Math.min(640, parent ? parent.width - 40 : 640)
    height: Math.min(460, parent ? parent.height - 40 : 460)
    title: qsTr("Message of the commit")
    standardButtons: Dialog.Ok | Dialog.Cancel
    onOpened: editor.focusTitle()
    onAccepted: clusters.setClusterMessage(cluster, editor.message())

    Binding {
        target: dialog.standardButton(Dialog.Ok)
        property: "enabled"
        value: editor.titleAccepted
    }
    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: dialog.visible && editor.editing && editor.titleAccepted
        onActivated: dialog.accept()
    }

    MessageEditor {
        id: editor
        anchors.fill: parent
        namePrefix: "cluster"
        monoFont: dialog.monoFont
    }
}
