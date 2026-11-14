pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// A file through its history in a window of its own (TimelapseView)
ApplicationWindow {
    id: window

    required property string workDir
    required property string path
    required property font monoFont

    width: 1100
    height: 800
    visible: true
    title: qsTr("Time-lapse: %1").arg(path)

    TimelapseView {
        anchors.fill: parent
        workDir: window.workDir
        path: window.path
        monoFont: window.monoFont
        active: window.active
    }
}
