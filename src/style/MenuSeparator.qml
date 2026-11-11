import QtQuick
import QtQuick.Templates as T

T.MenuSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    leftPadding: -Primitives.spacing.sp050
    rightPadding: -Primitives.spacing.sp050
    verticalPadding: Primitives.spacing.sp050

    contentItem: Rectangle {
        implicitWidth: 192
        implicitHeight: 1
        color: TokenInterface.semantics.stroke_subtle
    }
}
