import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

// An input with small minus and plus buttons inside on the right; each
// button is a part of the field's style with its own state.
//
// leadingIcon (an SVG) or leadingItem stands before the value, unit right
// after it, toned down ("pt"); both parts of the field's style.
T.SpinBox {
    id: control

    property list<Style> styles
    property int groupPosition: GroupStyle.Position.None
    property bool error: false
    readonly property alias styleCascade: _style
    readonly property int stepWidth: _upStyle.width

    property url leadingIcon
    property alias leadingItem: _leadingItems.data
    property string unit

    StyleCascade {
        id: _style
        base: [FieldStyle.base, FieldStyle.spinBox]
        styles: control.styles
        overrides: [control.activeFocus ? FieldStyle.focus : null,
                    control.error ? FieldStyle.error : null,
                    GroupStyle.of(control.groupPosition)]
        state: ControlState.hoverable(control)
    }
    StyleCascade {
        id: _leadingStyle
        parentCascade: _style
        part: StylePart.Leading
    }
    StyleCascade {
        id: _unitStyle
        parentCascade: _style
        part: StylePart.Unit
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             up.implicitIndicatorHeight, down.implicitIndicatorHeight)

    leftPadding: _leading.visible
                 ? _leadingStyle.padding.left + _leading.width + _leadingStyle.padding.right
                 : _style.padding.left
    rightPadding: 2 * stepWidth + 2 * Primitives.spacing.sp050
                  + (_unit.visible ? _unitStyle.padding.left + _unit.implicitWidth : 0)
    hoverEnabled: true
    opacity: _style.opacity
    // In a group over its neighbours while focused, so its border shows
    z: activeFocus ? 2 : 0

    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        clip: width < implicitWidth
        font: control.font
        color: _style.text.color
        selectionColor: _style.selection
        selectedTextColor: _style.selectedText
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
    }

    down.indicator: Rectangle {
        id: downIndicator
        x: control.mirrored ? control.stepWidth + Primitives.spacing.sp050
                            : control.width - 2 * control.stepWidth - Primitives.spacing.sp050
        y: (control.height - height) / 2
        implicitWidth: _downStyle.width
        implicitHeight: _downStyle.width
        radius: _downStyle.radius.value
        color: _downStyle.background

        StyleCascade {
            id: _downStyle
            parentCascade: _style
            part: StylePart.Indicator
            state: !downIndicator.enabled ? StyleState.Disable
                   : control.down.pressed ? StyleState.Active
                   : control.down.hovered ? StyleState.Hover : StyleState.Idle
        }

        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 1.5
            radius: 1
            color: _downStyle.text.color
        }
    }

    up.indicator: Rectangle {
        id: upIndicator
        x: control.mirrored ? Primitives.spacing.sp050 : control.width - control.stepWidth - Primitives.spacing.sp050
        y: (control.height - height) / 2
        implicitWidth: _upStyle.width
        implicitHeight: _upStyle.width
        radius: _upStyle.radius.value
        color: _upStyle.background

        StyleCascade {
            id: _upStyle
            parentCascade: _style
            part: StylePart.Indicator
            state: !upIndicator.enabled ? StyleState.Disable
                   : control.up.pressed ? StyleState.Active
                   : control.up.hovered ? StyleState.Hover : StyleState.Idle
        }

        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 1.5
            radius: 1
            color: _upStyle.text.color
        }
        Rectangle {
            anchors.centerIn: parent
            width: 1.5
            height: 10
            radius: 1
            color: _upStyle.text.color
        }
    }

    Item {
        id: _leading
        // Left to right only, as igitt is in English; the unit too
        x: _leadingStyle.padding.left
        anchors.verticalCenter: parent.verticalCenter
        width: _leadingStyle.width
        height: _leadingStyle.icon.size
        visible: control.leadingIcon.toString() !== "" || _leadingItems.children.length > 0

        IconImage {
            anchors.centerIn: parent
            width: _leadingStyle.icon.size
            height: _leadingStyle.icon.size
            visible: control.leadingIcon.toString() !== ""
            source: control.leadingIcon
            color: _leadingStyle.icon.color
        }
        Item {
            id: _leadingItems
            anchors.fill: parent
        }
    }

    // Right after the value
    Text {
        id: _unit
        visible: control.unit !== ""
        text: control.unit
        font: control.font
        color: _unitStyle.text.color
        anchors.verticalCenter: parent.verticalCenter
        x: control.leftPadding + Math.min(control.contentItem.contentWidth, control.availableWidth)
           + _unitStyle.padding.left
    }

    background: StyledBackground {
        cascade: _style
    }
}
