import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: control

    StyleCascade {
        id: _style
        base: [ScrollBarStyle.base]
        overrides: [control.interactive ? null : ScrollBarStyle.thin]
        state: control.pressed ? StyleState.Active
               : control.hovered ? StyleState.Hover : StyleState.Idle
    }
    StyleCascade {
        id: _handleStyle
        parentCascade: _style
        part: StylePart.Handle
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: _style.padding.value
    visible: control.policy !== T.ScrollBar.AlwaysOff
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height

    contentItem: Rectangle {
        implicitWidth: _handleStyle.width
        implicitHeight: _handleStyle.width
        radius: width / 2
        color: _handleStyle.background
        opacity: control.policy === T.ScrollBar.AlwaysOn || (control.active && control.size < 1.0) ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
}
