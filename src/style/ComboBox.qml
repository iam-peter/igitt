import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

// Select: an input-like trigger with a faint chevron; the list is
// a popover with rounded items and a check at the selected one
T.ComboBox {
    id: control

    property list<Style> styles
    property int groupPosition: GroupStyle.Position.None
    property bool error: false
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [FieldStyle.base, FieldStyle.comboBox]
        styles: control.styles
        overrides: [control.activeFocus ? FieldStyle.focus : null,
                    control.error ? FieldStyle.error : null,
                    GroupStyle.of(control.groupPosition)]
        state: ControlState.hoverable(control)
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    // Editable, the text field inside takes the left padding, so a click
    // there places the cursor
    leftPadding: control.editable ? 0 : _style.padding.left
    rightPadding: _style.padding.right + (indicator ? indicator.width + _style.spacing : 0)
    hoverEnabled: true
    // Editable, the text is selected by dragging as in any text field (Qt
    // leaves it off)
    selectTextByMouse: true
    opacity: _style.opacity
    // In a group over its neighbours while focused, so its border shows
    z: activeFocus ? 2 : 0

    delegate: T.ItemDelegate {
        id: item
        required property var model
        required property int index

        // Lights up only as the list's highlighted row, which follows
        // the pointer and the keys. As large as the combo box's size has
        // it (its Item part); the room for the tick comes after that.
        StyleCascade {
            id: _itemStyle
            base: [ItemStyle.base]
            parentCascade: _style
            part: StylePart.Item
            overrides: [ItemStyle.comboItem, item.highlighted ? ItemStyle.highlighted : null]
            state: !item.enabled ? StyleState.Disable : StyleState.Idle
        }

        width: ListView.view.width
        implicitHeight: _itemStyle.height
        leftPadding: _itemStyle.padding.left
        rightPadding: _itemStyle.padding.right
        hoverEnabled: control.hoverEnabled
        highlighted: control.highlightedIndex === index
        text: model[control.textRole] ?? ""

        contentItem: Text {
            text: item.text
            font: item.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            color: _itemStyle.text.color
        }
        background: Rectangle {
            radius: _itemStyle.radius.value
            color: _itemStyle.background
            ColorImage {
                anchors.right: parent.right
                anchors.rightMargin: _itemStyle.spacing
                anchors.verticalCenter: parent.verticalCenter
                visible: control.currentIndex === item.index
                source: "qrc:/qt/qml/IgittStyle/images/check.svg"
                sourceSize: Qt.size(_itemStyle.icon.size, _itemStyle.icon.size)
                color: _itemStyle.icon.color
            }
        }
    }

    indicator: ColorImage {
        x: control.mirrored ? _style.padding.right : control.width - width - _style.padding.right
        y: (control.height - height) / 2
        source: "qrc:/qt/qml/IgittStyle/images/chevron-down.svg"
        sourceSize: Qt.size(_arrowStyle.icon.size, _arrowStyle.icon.size)
        color: _arrowStyle.icon.color
        opacity: _arrowStyle.opacity

        StyleCascade {
            id: _arrowStyle
            parentCascade: _style
            part: StylePart.Arrow
        }
        // Points up while the list is open
        rotation: control.popup.visible ? 180 : 0
    }

    contentItem: T.TextField {
        leftPadding: control.editable ? _style.padding.left : 0
        topPadding: _style.padding.top
        bottomPadding: _style.padding.bottom

        text: control.editable ? control.editText : control.displayText

        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse

        font: control.font
        color: _style.text.color
        selectionColor: _style.selection
        selectedTextColor: _style.selectedText
        verticalAlignment: Text.AlignVCenter
    }

    background: StyledBackground {
        cascade: _style
    }

    // Nothing to choose: the list does not show at all
    popup: T.Popup {
        id: list

        // A click outside only closes the list
        modal: true
        dim: false
        // Keeps the drag handlers of the window behind off while shown
        readonly property PopupHold popupHold: PopupHold {
            popup: list
        }
        onOpened: if (control.count === 0) close()
        y: control.height + _listStyle.spacing
        width: control.width
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         control.Window.height - topMargin - bottomMargin)
        topMargin: 2 * _listStyle.spacing
        bottomMargin: 2 * _listStyle.spacing
        padding: _listStyle.padding.value

        StyleCascade {
            id: _listStyle
            base: [PopupStyle.list]
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            visible: control.count > 0
            color: _listStyle.background
            radius: _listStyle.radius.value
            border.width: _listStyle.border.width
            border.color: _listStyle.border.color
            Shadow {
                z: -1
                anchors.fill: parent
                radius: parent.radius
                depth: 3
            }
        }
    }
}
