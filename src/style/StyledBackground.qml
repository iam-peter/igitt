import QtQuick

// A control's background as its style resolves it: color, border, and a
// radius per corner, so controls in a group share square inner edges
Rectangle {
    required property StyleCascade cascade

    implicitWidth: cascade.width
    implicitHeight: cascade.height
    color: cascade.background
    border.width: cascade.border.width
    border.color: cascade.border.color
    radius: cascade.radius.value
    // A corner only where it differs: set, Qt draws the rectangle another
    // way, a little differently at every corner
    topLeftRadius: corner(cascade.radius.topLeft)
    topRightRadius: corner(cascade.radius.topRight)
    bottomLeftRadius: corner(cascade.radius.bottomLeft)
    bottomRightRadius: corner(cascade.radius.bottomRight)

    function corner(r: int): var {
        return r === cascade.radius.value ? undefined : r
    }
}
