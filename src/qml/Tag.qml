import QtQuick
import QtQuick.Controls
import IgittStyle as Styles

// A small marker next to a commit subject, like "planned".
Rectangle {
    id: tag

    property alias text: label.text
    property color tint
    property bool filled: false

    implicitWidth: label.implicitWidth + 10
    implicitHeight: label.implicitHeight + 2
    radius: 3
    color: filled ? tint : Styles.TokenInterface.transparent
    border.width: filled ? 0 : 1.5
    border.color: tint

    Label {
        id: label
        anchors.centerIn: parent
        color: tag.filled ? Styles.TokenInterface.textOn(tag.tint) : tag.tint
        font.pixelSize: 11
        font.bold: !tag.filled
    }
}
