import QtQuick
import QtQuick.Templates as T

// A bar of tool buttons on the window's color, a line below: as high as
// a button and 4 px around it
T.ToolBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    background: Rectangle {
        implicitHeight: Primitives.size.s32 + 2 * Primitives.spacing.sp050
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
