pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Igitt.Core

// Composing commits in a window of its own (ComposeView)
ApplicationWindow {
    id: window

    required property Repository repo
    required property font monoFont
    required property var setPlanning
    property var editCommit: null
    property var rewordCommit: null

    width: 1300
    height: 850
    visible: true
    title: qsTr("Compose commits — %1").arg(repo.path)

    ComposeView {
        objectName: "composeView"
        anchors.fill: parent
        repo: window.repo
        monoFont: window.monoFont
        setPlanning: window.setPlanning
        editCommit: window.editCommit
        rewordCommit: window.rewordCommit
        active: window.active
    }
}
