pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt

Control {
    id: delegate

    required property int index
    required property string sha
    required property string shortSha
    required property string subject
    required property string author
    required property string date
    required property var refs
    required property var graph
    required property bool isHead

    // Set while lines are dragged over this row: the empty string when
    // they can be folded into it, otherwise why not.
    property bool dropTarget: false
    property string dropBlocker

    readonly property bool current: ListView.isCurrentItem
    readonly property color textColor: current ? palette.highlightedText : palette.text

    signal clicked()
    signal contextMenuRequested()

    implicitHeight: 24
    leftPadding: 4
    rightPadding: 8

    background: Rectangle {
        color: delegate.current ? delegate.palette.highlight
                                : delegate.index % 2 ? delegate.palette.alternateBase : delegate.palette.base

        Rectangle {
            anchors.fill: parent
            visible: delegate.dropTarget
            color: delegate.dropBlocker === "" ? Qt.rgba(0.2, 0.7, 0.3, 0.25) : Qt.rgba(0.85, 0.2, 0.2, 0.2)
            border.width: 2
            border.color: delegate.dropBlocker === "" ? "#59a14f" : "#e15759"
        }
    }

    ToolTip.visible: dropTarget
    ToolTip.text: dropBlocker === "" ? qsTr("Fold into \"%1\"").arg(subject) : dropBlocker

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
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: delegate.contextMenuRequested()
    }
}
