import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

// Input: bordered, rounded, the focus color on the border when focused.
//
// leadingIcon (an SVG) or leadingItem stands before the text, trailingItems
// after it (a clear button), unit right after the value ("cm"); each is a
// part of the field's style with its own margins.
T.TextField {
    id: control

    property list<Style> styles
    property int groupPosition: GroupStyle.Position.None
    // The input cannot be used: shown in the border
    property bool error: false
    readonly property alias styleCascade: _style

    property url leadingIcon
    property alias leadingItem: _leadingItems.data
    property alias trailingItems: _trailing.data
    property string unit

    StyleCascade {
        id: _style
        base: [FieldStyle.base, FieldStyle.textField]
        styles: control.styles
        overrides: [control.activeFocus && !control.readOnly ? FieldStyle.focus : null,
                    control.readOnly ? FieldStyle.readOnly : null,
                    control.width > 0 && control.width < 64 ? FieldStyle.narrow : null,
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
        id: _trailingStyle
        parentCascade: _style
        part: StylePart.Trailing
    }
    StyleCascade {
        id: _unitStyle
        parentCascade: _style
        part: StylePart.Unit
    }

    // With the normal padding, not the actual one, which depends on the width
    implicitWidth: implicitBackgroundWidth + leftInset + rightInset
                   || Math.max(contentWidth, _placeholder.implicitWidth) + 24
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             _placeholder.implicitHeight + topPadding + bottomPadding)

    leftPadding: _leading.visible
                 ? _leadingStyle.padding.left + _leading.width + _leadingStyle.padding.right
                 : _style.padding.left
    // A trailing item can hide itself (a clear button without text): the
    // row is then 0 px wide
    rightPadding: (_trailing.width > 0
                   ? _trailingStyle.padding.left + _trailing.width + _trailingStyle.padding.right
                   : _style.padding.right)
                  + (_unit.visible ? _unitStyle.padding.left + _unit.implicitWidth : 0)
    topPadding: _style.padding.top
    bottomPadding: _style.padding.bottom
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true
    opacity: _style.opacity
    // In a group over its neighbours while focused, so its border shows
    z: activeFocus ? 2 : 0

    color: _style.text.color
    selectionColor: _style.selection
    selectedTextColor: _style.selectedText
    placeholderTextColor: _style.placeholder

    PlaceholderText {
        id: _placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)

        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        visible: !control.length && !control.preeditText && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
        elide: Text.ElideRight
        renderType: control.renderType
    }

    Item {
        id: _leading
        // Left to right only, as igitt is in English: a text field is no
        // Control and has no mirrored
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

    Row {
        id: _trailing
        anchors.right: parent.right
        anchors.rightMargin: _trailingStyle.padding.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: _trailingStyle.spacing
    }

    // Right after the text, wherever the alignment puts it
    Text {
        id: _unit
        visible: control.unit !== ""
        text: control.unit
        font: control.font
        color: _unitStyle.text.color
        anchors.verticalCenter: parent.verticalCenter
        // A text field is no Control: no availableWidth of its own
        readonly property real room: control.width - control.leftPadding - control.rightPadding
        x: control.leftPadding + _unitStyle.padding.left
           + (control.effectiveHorizontalAlignment === TextInput.AlignHCenter
              ? (room + Math.min(control.contentWidth, room)) / 2
              : control.effectiveHorizontalAlignment === TextInput.AlignRight
                ? room : Math.min(control.contentWidth, room))
    }

    background: StyledBackground {
        cascade: _style
    }
}
