import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.MenuItem {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [ItemStyle.base, ItemStyle.menuItem]
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
        readonly property real arrowPadding: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        readonly property real indicatorPadding: control.checkable && control.indicator ? control.indicator.width + control.spacing : 0
        leftPadding: !control.mirrored ? indicatorPadding : arrowPadding
        rightPadding: control.mirrored ? indicatorPadding : arrowPadding

        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: Qt.AlignLeft

        icon.source: control.icon.source
        icon.width: control.icon.width
        icon.height: control.icon.height
        icon.color: _style.icon.color
        text: control.text
        font: control.font
        color: _style.text.color
    }

    indicator: ColorImage {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.checked
        source: control.checkable ? "qrc:/qt/qml/IgittStyle/images/check.svg" : ""
        sourceSize: Qt.size(16, 16)
        color: _style.text.color
    }

    arrow: ColorImage {
        x: control.mirrored ? control.leftPadding : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.subMenu
        mirror: control.mirrored
        source: control.subMenu ? "qrc:/qt/qml/IgittStyle/images/chevron-right.svg" : ""
        sourceSize: Qt.size(16, 16)
        color: _style.text.color
    }

    // implicitWidth is a minimum only: entries are as wide as their text
    // needs, and a menu as wide as its longest entry
    background: StyledBackground {
        cascade: _style
    }
}
