import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.RadioButton {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [ToggleStyle.base, ToggleStyle.radioButton]
        styles: control.styles
        overrides: [control.visualFocus ? ToggleStyle.focus : null]
        state: ControlState.hoverable(control)
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: _style.padding.value
    spacing: _style.spacing
    opacity: _style.opacity

    indicator: Rectangle {
        implicitWidth: _indicatorStyle.width
        implicitHeight: _indicatorStyle.width
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: _indicatorStyle.radius.value
        color: _indicatorStyle.background
        border.width: _indicatorStyle.border.width
        border.color: _indicatorStyle.border.color

        StyleCascade {
            id: _indicatorStyle
            parentCascade: _style
            part: StylePart.Indicator
        }

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            color: _indicatorStyle.accent
            visible: control.checked
        }
    }

    // The room an icon before the label takes, with its gap to the text
    readonly property real _iconRoom: control.icon.source.toString() !== ""
                                      ? _iconStyle.icon.size + _iconStyle.spacing : 0

    StyleCascade {
        id: _iconStyle
        parentCascade: _style
        part: StylePart.Icon
    }

    contentItem: CheckLabel {
        leftPadding: (control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0)
                     + (control.mirrored ? 0 : control._iconRoom)
        rightPadding: (control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0)
                      + (control.mirrored ? control._iconRoom : 0)
        text: control.text
        font: control.font
        color: _style.text.color

        IconImage {
            visible: control._iconRoom > 0
            x: control.mirrored ? parent.width - parent.rightPadding + _iconStyle.spacing
                                : parent.leftPadding - control._iconRoom
            anchors.verticalCenter: parent.verticalCenter
            width: _iconStyle.icon.size
            height: _iconStyle.icon.size
            source: control.icon.source
            color: _iconStyle.icon.color
        }
    }
}
