import QtQuick
import QtQuick.Templates as T

// A popover
T.Popup {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [PopupStyle.popover]
        styles: control.styles
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: _style.padding.value

    // Blocks the window behind it, without dimming it: a click outside
    // only closes it, and does not also press what lies below
    modal: true
    dim: false

    background: Rectangle {
        color: _style.background
        radius: _style.radius.value
        border.width: _style.border.width
        border.color: _style.border.color
        Shadow {
            z: -1
            anchors.fill: parent
            radius: parent.radius
            depth: 3
        }
    }

    T.Overlay.modal: Rectangle {
        color: _style.dim
    }
}
