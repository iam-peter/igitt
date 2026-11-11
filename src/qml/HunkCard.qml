pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import IgittStyle as Styles

// A hunk on a card: its file and line, how much it adds and removes, and
// its first changed lines. tint marks the cluster it is in.
//
// Split open (the scissors, splitting), the card shows all its lines:
// dragging over them picks them, a click picks or drops one, Shift+click
// a range; dragging what is picked carries those lines out, to wherever
// the view lets them go (carryStarted, carryMoved, carryEnded).
Rectangle {
    id: card

    required property var hunk
    required property font monoFont
    property color tint: Styles.TokenInterface.transparent
    // Lines of the diff shown; 0 shows only the title.
    property int maxLines: 4
    property bool showPath: true
    // A label in the title row, such as the group the hunk is in
    property string badge
    // Whether the scissors show, for a card of more than one line
    property bool splittable: (hunk.lines?.length ?? 0) > 1
    // Split open: every line shows, to be picked and carried out. Set by
    // the view, which keeps it while the card is made anew.
    property bool splitting: false
    // The picked lines, by their index in the hunk
    property var picked: []
    // The picked lines are on their way out
    property bool carrying: false
    // Lines shown at most while split open; more scroll
    property int maxSplitLines: 24

    // The scissors: open or close the card's lines
    signal splitRequested()
    // The picked lines leave the card, as a piece of their own: the card's
    // piece with only those lines, and pick, their indexes. Positions are
    // the card's.
    signal carryStarted(var piece)
    signal carryMoved(point position)
    signal carryEnded()

    readonly property Styles.Semantics s: Styles.TokenInterface.semantics
    // The changed lines first: the context around them says little here.
    readonly property var shownLines: {
        const changed = hunk.lines.filter(line => line.kind !== " ")
        return splitting ? changed : changed.slice(0, maxLines)
    }
    readonly property int lineHeight: Math.ceil(metrics.height)

    // The piece the picked lines make
    function carriedPiece(): var {
        const lines = hunk.lines.filter(line => picked.includes(line.index))
        return Object.assign({}, hunk, {
            lines: lines,
            pick: Array.from(picked),
            added: lines.filter(line => line.kind === "+").length,
            removed: lines.filter(line => line.kind === "-").length,
            split: true,
        })
    }
    // The row of the shown lines at a height of the list, the nearest one
    // above or below it
    function rowAt(y: real): int {
        if (shownLines.length === 0)
            return -1
        const row = Math.floor((y + lineList.contentY) / lineHeight)
        return Math.max(0, Math.min(shownLines.length - 1, row))
    }
    function pickRange(from: int, to: int) {
        const picks = []
        for (let row = Math.min(from, to); row <= Math.max(from, to); ++row)
            picks.push(shownLines[row].index)
        picked = picks
    }
    function toggle(row: int) {
        const index = shownLines[row].index
        picked = picked.includes(index) ? picked.filter(i => i !== index) : picked.concat([index])
    }

    onSplittingChanged: {
        picked = []
        lineList.contentY = 0
    }

    implicitWidth: 260
    implicitHeight: content.implicitHeight + 12
    radius: 4
    color: s.background_elevated
    border.width: splitting ? 2 : 1
    border.color: splitting ? s.stroke_focus : s.stroke_subtle

    FontMetrics {
        id: metrics
        font.family: card.monoFont.family
        font.pixelSize: 11
    }

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
                text: (card.showPath ? `${card.hunk.path}:${card.hunk.line}` : qsTr("line %1").arg(card.hunk.line))
                      + (card.hunk.split ? qsTr(" (part)") : "")
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                font.bold: true
                font.pixelSize: 12
            }
            Rectangle {
                visible: card.badge !== ""
                Layout.maximumWidth: card.width * 0.55
                implicitWidth: badgeLabel.implicitWidth + 10
                implicitHeight: badgeLabel.implicitHeight + 2
                radius: 3
                color: card.tint
                Label {
                    id: badgeLabel
                    anchors.fill: parent
                    horizontalAlignment: Text.AlignHCenter
                    text: card.badge
                    elide: Text.ElideRight
                    color: Styles.TokenInterface.textOn(card.tint)
                    font.pixelSize: 10
                }
            }
            IconButton {
                objectName: "splitButton"
                visible: card.splittable || card.splitting
                iconName: card.splitting ? "x" : "scissors"
                implicitWidth: 20
                implicitHeight: 20
                onClicked: card.splitRequested()
                ToolTip.visible: hovered
                ToolTip.text: card.splitting ? qsTr("Done splitting (Esc)")
                                             : qsTr("Split: pick lines and drag them into another cluster")
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
            visible: card.hunk.section !== "" && !card.splitting
            text: card.hunk.section
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: card.s.text_muted
            font.family: card.monoFont.family
            font.pixelSize: 11
        }
        Label {
            Layout.fillWidth: true
            visible: card.splitting
            wrapMode: Text.Wrap
            text: card.picked.length === 0
                  ? qsTr("Drag over lines to pick them")
                  : qsTr("%n line(s) picked: drag them onto a cluster", "", card.picked.length)
            color: card.s.text_muted
            font.pixelSize: 11
        }

        // The lines, and where they are picked and carried out
        Item {
            id: linesArea
            objectName: "hunkLines"

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(card.shownLines.length,
                                             card.splitting ? card.maxSplitLines : card.shownLines.length)
                                    * card.lineHeight
            visible: card.shownLines.length > 0

            ListView {
                id: lineList

                anchors.fill: parent
                clip: true
                interactive: false
                boundsBehavior: Flickable.StopAtBounds
                model: card.shownLines
                ScrollBar.vertical: ScrollBar {
                    visible: card.splitting && lineList.contentHeight > lineList.height
                }
                delegate: Rectangle {
                    id: line

                    required property var modelData
                    readonly property bool isPicked: card.picked.includes(modelData.index)

                    width: ListView.view.width
                    height: card.lineHeight
                    color: modelData.kind === "+" ? card.s.diff_added : card.s.diff_removed
                    opacity: isPicked && card.carrying ? 0.35 : 1

                    // Picked: tinted, and marked at the edge
                    Rectangle {
                        anchors.fill: parent
                        visible: line.isPicked
                        color: Qt.alpha(card.s.primary_default, 0.28)
                        Rectangle {
                            width: 2
                            height: parent.height
                            color: card.s.primary_default
                        }
                    }

                    Text {
                        width: parent.width
                        height: parent.height
                        leftPadding: 2
                        verticalAlignment: Text.AlignVCenter
                        text: line.modelData.kind + line.modelData.text
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: card.s.text_default
                        font: metrics.font
                    }
                }
            }
            // The list scrolls by the wheel only, as dragging picks
            WheelHandler {
                enabled: card.splitting
                target: null
                onWheel: event => {
                    lineList.contentY = Math.max(0, Math.min(lineList.contentHeight - lineList.height,
                                                             lineList.contentY - event.angleDelta.y / 2))
                }
            }
            HoverHandler {
                id: hover
                enabled: card.splitting
                cursorShape: card.picked.includes(card.shownLines[card.rowAt(hover.point.position.y)]?.index)
                             ? Qt.OpenHandCursor : Qt.IBeamCursor
            }
            TapHandler {
                id: tap

                property int anchorRow: -1

                enabled: card.splitting
                // The handler's own point has the modifiers; the event
                // point it passes does not
                onTapped: {
                    const row = card.rowAt(tap.point.position.y)
                    if ((tap.point.modifiers & Qt.ShiftModifier) && anchorRow >= 0) {
                        card.pickRange(anchorRow, row)
                    } else {
                        card.toggle(row)
                        anchorRow = row
                    }
                }
            }
            // From a picked line, the drag carries the picked lines out;
            // from any other, it picks the lines it goes over
            DragHandler {
                id: sweep

                property bool carry
                property int anchorRow

                target: null
                enabled: card.splitting && !(parent.Window.window?.popupOpen ?? false)
                // A list does not take the drag over to scroll
                grabPermissions: PointerHandler.CanTakeOverFromAnything
                cursorShape: carry ? Qt.ClosedHandCursor : Qt.IBeamCursor
                onActiveChanged: {
                    if (active) {
                        const row = card.rowAt(centroid.pressPosition.y)
                        carry = card.picked.includes(card.shownLines[row].index)
                        if (carry) {
                            card.carrying = true
                            card.carryStarted(card.carriedPiece())
                            card.carryMoved(linesArea.mapToItem(card, centroid.position))
                        } else {
                            anchorRow = row
                            tap.anchorRow = row
                            card.pickRange(row, card.rowAt(centroid.position.y))
                        }
                    } else if (carry) {
                        carry = false
                        card.carrying = false
                        card.carryEnded()
                    }
                }
                onCentroidChanged: {
                    if (!active)
                        return
                    if (carry) {
                        card.carryMoved(linesArea.mapToItem(card, centroid.position))
                        return
                    }
                    // Past the ends, the list scrolls along
                    const y = centroid.position.y
                    if (y < 0)
                        lineList.contentY = Math.max(0, lineList.contentY + y / 4)
                    else if (y > linesArea.height)
                        lineList.contentY = Math.min(Math.max(0, lineList.contentHeight - lineList.height),
                                                     lineList.contentY + (y - linesArea.height) / 4)
                    card.pickRange(anchorRow, card.rowAt(y))
                }
            }
        }
        Label {
            visible: !card.splitting && card.hunk.added + card.hunk.removed > card.shownLines.length
                     && card.maxLines > 0
            text: qsTr("… %n more line(s)", "", card.hunk.added + card.hunk.removed - card.shownLines.length)
            color: card.s.text_muted
            font.pixelSize: 11
        }
    }
}
