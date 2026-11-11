import QtQuick

// A soft drop shadow from a few translucent rounded rectangles; depth 1
// is shadow-xs, 2 shadow-sm, 3 shadow-md, 4 shadow-lg. Put it into a
// background with z: -1 and anchors.fill: parent.
Item {
    id: shadow

    property real radius: 0
    property int depth: 1

    Repeater {
        model: shadow.depth + 1
        Rectangle {
            required property int index
            anchors.fill: parent
            anchors.leftMargin: -index
            anchors.rightMargin: -index
            anchors.topMargin: -index + shadow.depth * 0.5
            anchors.bottomMargin: -index - shadow.depth
            radius: shadow.radius + index
            color: Qt.rgba(0, 0, 0, TokenInterface.semantics.shadow_strength / (index + 1))
        }
    }
}
