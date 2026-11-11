import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.ItemDelegate {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [ItemStyle.base, ItemStyle.delegate]
        styles: control.styles
        overrides: [control.highlighted ? ItemStyle.highlighted : null]
        state: ControlState.item(control)
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    topPadding: _style.padding.top
    bottomPadding: _style.padding.bottom
    leftPadding: _style.padding.left
    rightPadding: _style.padding.right
    spacing: _style.spacing
    hoverEnabled: true
    opacity: _style.opacity

    icon.width: _style.icon.size
    icon.height: _style.icon.size

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: control.display === IconLabel.IconOnly || control.display === IconLabel.TextUnderIcon
                   ? Qt.AlignCenter : Qt.AlignLeft

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
