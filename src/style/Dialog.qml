import QtQuick
import QtQuick.Templates as T

// Dialog: a rounded, bordered card with a large shadow, the title
// in semibold, the buttons on the right
T.Dialog {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [PopupStyle.dialog]
        styles: control.styles
        overrides: [control.modal ? null : PopupStyle.modeless]
    }
    StyleCascade {
        id: _titleStyle
        parentCascade: _style
        part: StylePart.Label
    }

    // The title's width from its text: an elided label's own natural
    // width follows the width it gets, from here, which made a loop
    TextMetrics {
        id: titleMetrics
        font: control.header ? control.header.font : control.font
        text: control.title
    }
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            control.header?.visible
                                ? Math.ceil(titleMetrics.advanceWidth) + control.header.leftPadding
                                  + control.header.rightPadding
                                : 0,
                            implicitFooterWidth)
    // Whole pixels, like the title's height below: the buttons stand at
    // the dialog's height less theirs, and between pixels every line in
    // them is blurred
    implicitHeight: Math.ceil(Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                       implicitContentHeight + topPadding + bottomPadding
                                       + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                                       + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0)))

    leftPadding: _style.padding.left
    rightPadding: _style.padding.right
    topPadding: _style.padding.top
    bottomPadding: _style.padding.bottom

    background: Rectangle {
        color: _style.background
        radius: _style.radius.value
        border.width: _style.border.width
        border.color: _style.border.color
        Shadow {
            z: -1
            anchors.fill: parent
            radius: parent.radius
            depth: 4
        }
    }

    header: T.Label {
        text: control.title
        visible: parent?.parent === T.Overlay.overlay && control.title
        elide: Text.ElideRight
        // Whole pixels: the content starts below it, and a fractional line
        // height (Inter, unhinted) put all of it between pixels
        height: Math.ceil(implicitHeight)
        font.pixelSize: _titleStyle.text.size
        font.weight: _titleStyle.text.weight
        color: _titleStyle.text.color
        leftPadding: _titleStyle.padding.left
        rightPadding: _titleStyle.padding.right
        topPadding: _titleStyle.padding.top
        bottomPadding: _titleStyle.padding.bottom
    }

    footer: DialogButtonBox {
        visible: count > 0
    }

    T.Overlay.modal: Rectangle {
        color: _style.dim
    }

    T.Overlay.modeless: Rectangle {
        color: _style.dim
    }
}
