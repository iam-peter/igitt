import QtQuick
import QtQuick.Templates as T

T.ToolSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: vertical ? Primitives.spacing.sp050 : 0
    verticalPadding: vertical ? 0 : Primitives.spacing.sp050

    contentItem: Rectangle {
        implicitWidth: control.vertical ? 1 : 20
        implicitHeight: control.vertical ? 18 : 1
        color: TokenInterface.semantics.stroke_subtle
    }
}
