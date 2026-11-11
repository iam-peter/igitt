import QtQuick
import QtQuick.Templates as T

// A card
T.Frame {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: Primitives.spacing.sp200

    background: Rectangle {
        color: TokenInterface.semantics.background_elevated
        radius: Primitives.radius.r4
        border.color: TokenInterface.semantics.stroke_subtle
    }
}
