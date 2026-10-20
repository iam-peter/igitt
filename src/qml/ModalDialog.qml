import QtQuick
import QtQuick.Controls

// A centered modal dialog that clearly dims what is behind it.
Dialog {
    anchors.centerIn: parent
    modal: true

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.45)
    }
}
