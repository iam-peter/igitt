import QtQuick
import QtQuick.Templates as T

// A button: ButtonStyle.base with a variant. Pages pick one with styles,
// e.g. [ButtonStyle.ghost]; otherwise highlighted is primary, flat ghost
// and any other outline. In a group (groupPosition) its inner corners are
// square.
T.Button {
    id: control

    property list<Style> styles
    property int groupPosition: GroupStyle.Position.None
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [ButtonStyle.base, ButtonStyle.variant(control.highlighted, control.flat)]
        styles: control.styles
        overrides: [control.visualFocus ? ButtonStyle.focus : null,
                    control.text === "" ? ButtonStyle.iconOnly : null,
                    GroupStyle.of(control.groupPosition)]
        state: ControlState.pressable(control)
    }

    // Whole pixels: the text's width is fractional, and in a row (the
    // dialog buttons) every button after it would stand between pixels
    implicitWidth: Math.ceil(Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                      implicitContentWidth + leftPadding + rightPadding))
    implicitHeight: Math.ceil(Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                       implicitContentHeight + topPadding + bottomPadding))

    topPadding: _style.padding.top
    bottomPadding: _style.padding.bottom
    leftPadding: _style.padding.left
    rightPadding: _style.padding.right
    spacing: _style.spacing
    hoverEnabled: true
    opacity: _style.opacity
    font.weight: _style.text.weight
    // In a group the buttons lie over the fields, so every shared edge
    // has the buttons' color; with focus over all, so its border shows
    z: visualFocus ? 2 : groupPosition !== GroupStyle.Position.None ? 1 : 0

    icon.width: _style.icon.size
    icon.height: _style.icon.size

    contentItem: ButtonLabel {
        control: control
        cascade: _style
    }

    background: StyledBackground {
        cascade: _style
    }
}
