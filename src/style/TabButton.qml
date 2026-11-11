import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.TabButton {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [TabStyle.button]
        styles: control.styles
        overrides: [control.checked ? TabStyle.checked : null,
                    control.visualFocus ? TabStyle.focus : null]
        state: ControlState.item(control)
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    topPadding: _style.padding.top
    bottomPadding: _style.padding.bottom
    leftPadding: _style.padding.left
    rightPadding: _style.padding.right
    spacing: _style.spacing
    hoverEnabled: true
    opacity: _style.opacity
    font.weight: _style.text.weight

    icon.width: _style.icon.size
    icon.height: _style.icon.size

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display

        icon.source: control.icon.source
        icon.width: control.icon.width
        icon.height: control.icon.height
        icon.color: _style.icon.color
        text: control.text
        font: control.font
        color: _style.text.color
    }

    background: StyledBackground {
        cascade: _style
    }
}
