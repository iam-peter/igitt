import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.Switch {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [ToggleStyle.base, ToggleStyle.switchControl]
        styles: control.styles
        overrides: [control.visualFocus ? ToggleStyle.focus : null,
                    control.checked ? ToggleStyle.switchChecked : null]
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
        implicitWidth: _trackStyle.width
        implicitHeight: _trackStyle.height
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: _trackStyle.background
        border.width: _trackStyle.border.width
        border.color: _trackStyle.border.color

        StyleCascade {
            id: _trackStyle
            parentCascade: _style
            part: StylePart.Indicator
        }
        StyleCascade {
            id: _handleStyle
            parentCascade: _style
            part: StylePart.Handle
        }

        Rectangle {
            x: Math.max(1, Math.min(parent.width - width - 1, control.visualPosition * parent.width - width / 2))
            y: 1
            width: _handleStyle.width
            height: _handleStyle.width
            radius: width / 2
            color: _handleStyle.background
            Behavior on x { enabled: !control.down; NumberAnimation { duration: 110 } }
        }
    }

    contentItem: CheckLabel {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: _style.text.color
    }
}
