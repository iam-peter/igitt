import QtQuick
import QtQuick.Controls

Rectangle {
    id: label

    required property string name
    required property string kind // head, branch, remote, tag, stash
    required property bool current

    implicitWidth: text.implicitWidth + 10
    implicitHeight: text.implicitHeight + 2
    radius: 3
    color: {
        switch (kind) {
        case "branch": return "#59a14f"
        case "remote": return "#e08e2b"
        case "tag": return "#c9a227"
        case "head": return "#e15759"
        default: return "#888888"
        }
    }
    border.width: current ? 2 : 0
    border.color: Qt.darker(color, 1.6)

    Label {
        id: text
        anchors.centerIn: parent
        text: label.name
        textFormat: Text.PlainText
        color: "white"
        font.pixelSize: 11
        font.bold: label.current
    }
}
