import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

// Dropdown menu: a popover with rounded items
T.Menu {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    StyleCascade {
        id: _style
        base: [PopupStyle.menu]
        styles: control.styles
    }

    // Blocks the window behind it, without dimming it: a click outside
    // only closes it, and does not also press what lies below
    modal: true
    dim: false

    // As wide as its longest shown entry: the list itself has no natural
    // width, and a fixed one is wider than short entries need
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            entriesWidth() + leftPadding + rightPadding)

    function entriesWidth(): real {
        let w = 0
        for (let i = 0; i < count; ++i) {
            const entry = itemAt(i)
            if (entry && entry.visible)
                w = Math.max(w, entry.implicitWidth)
        }
        return w
    }
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: 0
    overlap: 1
    padding: _style.padding.value

    delegate: MenuItem {}

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
        boundsBehavior: Flickable.StopAtBounds
        ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: Rectangle {
        implicitWidth: _style.width
        implicitHeight: _style.height
        color: _style.background
        radius: _style.radius.value
        border.width: _style.border.width
        border.color: _style.border.color
        Shadow {
            z: -1
            anchors.fill: parent
            radius: parent.radius
            depth: 3
        }
    }

    T.Overlay.modal: Rectangle {
        color: _style.dim
    }
}
