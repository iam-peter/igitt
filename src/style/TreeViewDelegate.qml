import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

// A row of a tree: an ItemDelegate's row, indented by its depth, with a
// chevron in front of the rows that have children
T.TreeViewDelegate {
    id: control

    property list<Style> styles
    readonly property alias styleCascade: _style

    required property int row
    required property var model

    StyleCascade {
        id: _style
        base: [ItemStyle.base, ItemStyle.delegate]
        styles: control.styles
        overrides: [control.highlighted ? ItemStyle.highlighted : null]
        state: ControlState.item(control)
    }

    // Where the chevron starts and the content after it
    readonly property real __indicatorIndent: leftMargin + depth * indentation
    readonly property real __contentIndent: !isTreeNode ? 0
        : depth * indentation + (indicator ? indicator.width + spacing : 0)

    implicitWidth: leftMargin + __contentIndent + implicitContentWidth + rightPadding + rightMargin
    implicitHeight: Math.max(implicitBackgroundHeight,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    indentation: _style.icon.size
    leftMargin: _style.padding.left
    rightMargin: 0
    topPadding: _style.padding.top
    bottomPadding: _style.padding.bottom
    leftPadding: !mirrored ? leftMargin + __contentIndent
                           : width - leftMargin - __contentIndent - implicitContentWidth
    rightPadding: _style.padding.right
    spacing: _style.spacing
    hoverEnabled: true
    opacity: _style.opacity

    highlighted: selected || current

    indicator: Item {
        x: !control.mirrored ? control.__indicatorIndent
                             : control.width - control.__indicatorIndent - width
        y: (control.height - height) / 2
        implicitWidth: _style.icon.size
        implicitHeight: _style.icon.size

        ColorImage {
            anchors.centerIn: parent
            source: "qrc:/qt/qml/IgittStyle/images/chevron-right.svg"
            sourceSize: Qt.size(_style.icon.size, _style.icon.size)
            color: _style.icon.color
            opacity: 0.7
            rotation: control.expanded ? 90 : (control.mirrored ? 180 : 0)
        }
    }

    contentItem: Label {
        text: control.model.display
        elide: Text.ElideRight
        color: _style.text.color
    }

    background: StyledBackground {
        cascade: _style
    }
}
