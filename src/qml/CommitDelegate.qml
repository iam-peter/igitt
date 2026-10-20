pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt

Control {
    id: delegate

    required property int index
    required property string shortSha
    required property string subject
    required property string author
    required property string date
    required property var refs
    required property var graph
    required property bool isHead

    readonly property bool current: ListView.isCurrentItem
    readonly property color textColor: current ? palette.highlightedText : palette.text

    signal clicked()

    implicitHeight: 24
    leftPadding: 4
    rightPadding: 8

    background: Rectangle {
        color: delegate.current ? delegate.palette.highlight
                                : delegate.index % 2 ? delegate.palette.alternateBase : delegate.palette.base
    }

    contentItem: RowLayout {
        spacing: 6

        CommitGraph {
            Layout.fillHeight: true
            Layout.preferredWidth: implicitWidth
            row: delegate.graph
            isHead: delegate.isHead
            outlineColor: delegate.background.color
        }

        Repeater {
            model: delegate.refs
            delegate: RefLabel {
                required property var modelData
                name: modelData.name
                kind: modelData.kind
                current: modelData.current
            }
        }

        Label {
            Layout.fillWidth: true
            text: delegate.subject
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: delegate.textColor
            font.bold: delegate.isHead
        }
        Label {
            Layout.preferredWidth: 160
            text: delegate.author
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: delegate.textColor
        }
        Label {
            Layout.preferredWidth: 120
            text: delegate.date
            color: delegate.textColor
        }
        Label {
            Layout.preferredWidth: 90
            text: delegate.shortSha
            font.family: "monospace"
            color: delegate.textColor
        }
    }

    TapHandler {
        onTapped: delegate.clicked()
    }
}
