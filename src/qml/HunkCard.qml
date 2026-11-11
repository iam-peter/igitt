pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles

// A hunk on a card: its file and line, how much it adds and removes, and
// its first changed lines. tint marks the cluster it is in.
Rectangle {
    id: card

    required property var hunk
    required property font monoFont
    property color tint: Styles.TokenInterface.transparent
    // Lines of the diff shown; 0 shows only the title.
    property int maxLines: 4
    property bool showPath: true

    readonly property Styles.Semantics s: Styles.TokenInterface.semantics
    // The changed lines first: the context around them says little here.
    readonly property var shownLines: {
        const changed = hunk.lines.filter(line => line.kind !== " ")
        return changed.slice(0, maxLines)
    }

    implicitWidth: 260
    implicitHeight: content.implicitHeight + 12
    radius: 4
    color: s.background_elevated
    border.width: 1
    border.color: s.stroke_subtle

    // The cluster's color down the left edge
    Rectangle {
        width: 3
        height: parent.height - 8
        x: 3
        y: 4
        radius: 1.5
        color: card.tint
        visible: card.tint.a > 0
    }

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        anchors.leftMargin: 10
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: card.showPath ? `${card.hunk.path}:${card.hunk.line}` : qsTr("line %1").arg(card.hunk.line)
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                font.bold: true
                font.pixelSize: 12
            }
            Label {
                visible: card.hunk.added > 0
                text: `+${card.hunk.added}`
                color: card.s.success_default
                font.pixelSize: 11
            }
            Label {
                visible: card.hunk.removed > 0
                text: `−${card.hunk.removed}`
                color: card.s.danger_default
                font.pixelSize: 11
            }
        }
        Label {
            Layout.fillWidth: true
            visible: card.hunk.section !== ""
            text: card.hunk.section
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: card.s.text_muted
            font.family: card.monoFont.family
            font.pixelSize: 11
        }
        Repeater {
            model: card.shownLines
            delegate: Rectangle {
                id: line

                required property var modelData

                Layout.fillWidth: true
                implicitHeight: lineText.implicitHeight
                color: modelData.kind === "+" ? card.s.diff_added : card.s.diff_removed

                Text {
                    id: lineText
                    width: parent.width
                    leftPadding: 2
                    text: line.modelData.kind + line.modelData.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: card.s.text_default
                    font.family: card.monoFont.family
                    font.pixelSize: 11
                }
            }
        }
        Label {
            visible: card.hunk.added + card.hunk.removed > card.shownLines.length && card.maxLines > 0
            text: qsTr("… %n more line(s)", "", card.hunk.added + card.hunk.removed - card.shownLines.length)
            color: card.s.text_muted
            font.pixelSize: 11
        }
    }
}
