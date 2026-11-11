import QtQuick
import QtQuick.Templates as T

// A bar of tool buttons on the window's color, a line below: as high as
// what is in it, 4 px above and below, so a bar of small buttons is
// 32 px high and one of large ones 40 px
T.ToolBar {
    id: control

    topPadding: Primitives.spacing.sp050
    bottomPadding: Primitives.spacing.sp050

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    background: Rectangle {
        color: TokenInterface.semantics.background_default
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: TokenInterface.semantics.stroke_subtle
        }
    }
}
