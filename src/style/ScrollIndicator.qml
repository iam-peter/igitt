import QtQuick
import QtQuick.Templates as T

T.ScrollIndicator {
    id: control

    StyleCascade {
        id: _style
        base: [ScrollBarStyle.base]
        overrides: [ScrollBarStyle.thin]
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

    contentItem: Rectangle {
        implicitWidth: _handleStyle.width
        implicitHeight: _handleStyle.width
        radius: width / 2
        color: _handleStyle.background
        visible: control.size < 1.0
        opacity: control.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
}
