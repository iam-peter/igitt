import QtQuick
import QtQuick.Templates as T

// Tabs list: a muted rounded bar, the active trigger raised
T.TabBar {
    id: control

    property list<Style> styles

    StyleCascade {
        id: _style
        base: [TabStyle.bar]
        styles: control.styles
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    spacing: _style.spacing
    padding: _style.padding.value

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
        highlightMoveDuration: 0
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: 40
        preferredHighlightEnd: width - 40
    }

    background: Rectangle {
        color: _style.background
        radius: _style.radius.value
    }
}
