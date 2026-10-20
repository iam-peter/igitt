pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core

// One row of the commit graph, in the columns AppSettings.graph enables.
Control {
    id: delegate

    required property int index
    required property string sha
    required property string shortSha
    required property string subject
    required property string author
    required property string authorEmail
    required property string initials
    required property string date
    required property var refs
    required property var graph
    required property string laneColor
    required property bool isHead
    required property bool isMerge
    required property bool planned
    required property var stats
    required property bool matched
    required property var review

    required property Repository repo
    // Set while lines are dragged over this row: the empty string when
    // they can be folded into it, otherwise why not.
    property bool dropTarget: false
    property string dropBlocker
    // The selected lines most likely belong in this commit.
    property bool suggested: false

    readonly property var columns: AppSettings.graph
    readonly property bool current: ListView.isCurrentItem
    // As many ref labels as fit; the others are counted in a "+N" label.
    readonly property int shownRefs: Math.min(refs.length, Math.max(1, Math.floor((columns.refsWidth - 40) / 90)))

    signal clicked()
    signal contextMenuRequested(point position)

    implicitHeight: 30
    padding: 0
    hoverEnabled: true
    opacity: (matched ? 1 : 0.35) * (isMerge && columns.dimMerges ? 0.55 : 1)

    Component.onCompleted: {
        if (columns.showChanges && !stats)
            repo.requestStats(sha)
    }

    background: Rectangle {
        color: delegate.current ? Qt.alpha(delegate.laneColor, 0.3)
             : delegate.hovered ? Qt.alpha(delegate.laneColor, 0.12)
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
        spacing: 0

        // Branch / Tag
        Item {
            visible: delegate.columns.showRefs
            Layout.preferredWidth: delegate.columns.refsWidth
            Layout.fillHeight: true
            clip: true

            Row {
                id: labels
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Repeater {
                    model: delegate.refs.slice(0, delegate.shownRefs)
                    delegate: RefLabel {
                        required property var modelData
                        name: modelData.name
                        kind: modelData.kind
                        laneColor: modelData.color
                        current: modelData.current
                        remotes: modelData.remotes
                    }
                }
                RefLabel {
                    visible: delegate.refs.length > delegate.shownRefs
                    name: `+${delegate.refs.length - delegate.shownRefs}`
                    kind: "more"
                    laneColor: delegate.laneColor
                    current: false
                    remotes: delegate.refs.slice(delegate.shownRefs).map(ref => ref.name)
                }
            }
            // Leads from the labels to the commit node.
            Rectangle {
                visible: delegate.refs.length > 0
                x: labels.x + labels.width + 2
                width: Math.max(0, parent.width - x)
                height: 1.5
                anchors.verticalCenter: parent.verticalCenter
                color: Qt.alpha(delegate.laneColor, 0.6)
            }
        }

        CommitGraph {
            visible: delegate.columns.showGraph
            Layout.preferredWidth: delegate.columns.graphWidth
            Layout.fillHeight: true
            clip: true
            row: delegate.graph
            nodeKind: delegate.isMerge ? CommitGraph.Merge : delegate.isHead ? CommitGraph.Head : CommitGraph.Commit
            initials: delegate.initials
            showInitials: delegate.columns.showInitials
            connectLeft: delegate.refs.length > 0 && delegate.columns.showRefs
            outlineColor: delegate.palette.base
        }

        // Message
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 6

            Tag {
                visible: delegate.planned
                text: qsTr("planned")
                tint: "#8c5ad8"
                filled: true
            }
            // What Gerrit has for this commit, if it is pushed for review.
            Tag {
                readonly property string state: delegate.review ? delegate.review.state : ""

                visible: state !== ""
                text: delegate.review && delegate.review.number > 0
                      ? `Gerrit ${delegate.review.number}/${delegate.review.patchSet}` : qsTr("not on Gerrit")
                tint: state === "upToDate" ? "#59a14f"
                    : state === "outdated" ? "#e08e2b"
                    : state === "closed" ? "#888888" : "#4e79a7"
                ToolTip.visible: reviewHover.hovered
                ToolTip.text: state === "upToDate" ? qsTr("Gerrit has this commit as the current patch set.")
                            : state === "outdated" ? qsTr("Changed since the last upload; pushing adds a patch set.")
                            : state === "closed" ? qsTr("The change is merged or abandoned.")
                            : qsTr("Pushing for review creates a new change.")

                HoverHandler {
                    id: reviewHover
                    cursorShape: delegate.review && delegate.review.url ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
                TapHandler {
                    enabled: delegate.review !== undefined && delegate.review.url !== ""
                    onTapped: Qt.openUrlExternally(delegate.review.url)
                }
            }
            Tag {
                visible: delegate.suggested
                text: qsTr("fold here")
                tint: "#59a14f"
            }
            Label {
                Layout.fillWidth: true
                text: delegate.subject
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.bold: delegate.isHead
                font.italic: delegate.planned
            }
        }

        // Changes: files, and a bar of added (green) and removed (red) lines.
        Item {
            id: changes

            readonly property int added: delegate.stats ? delegate.stats.added : 0
            readonly property int removed: delegate.stats ? delegate.stats.removed : 0
            readonly property int total: added + removed
            // Logarithmic, so small commits still show and large ones fit.
            readonly property real barWidth: Math.max(0, width - fileCount.width - 16)
                                             * Math.min(1, Math.log10(total + 1) / 3.5)

            visible: delegate.columns.showChanges
            Layout.preferredWidth: delegate.columns.changesWidth
            Layout.fillHeight: true
            ToolTip.visible: changesHover.hovered && delegate.stats !== undefined
            ToolTip.text: delegate.stats ? qsTr("%n file(s)", "", delegate.stats.files)
                                           + `, +${added} −${removed}` : ""

            HoverHandler {
                id: changesHover
            }
            Label {
                id: fileCount
                x: 6
                width: 34
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                text: delegate.stats ? delegate.stats.files : ""
                opacity: 0.7
                font.pixelSize: 11
            }
            Row {
                x: fileCount.x + fileCount.width + 6
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: changes.total > 0 ? changes.barWidth * changes.added / changes.total : 0
                    height: 6
                    color: "#59a14f"
                }
                Rectangle {
                    width: changes.total > 0 ? changes.barWidth * changes.removed / changes.total : 0
                    height: 6
                    color: "#e15759"
                }
            }
        }

        Label {
            visible: delegate.columns.showAuthor
            Layout.preferredWidth: delegate.columns.authorWidth
            leftPadding: 6
            text: delegate.author
            textFormat: Text.PlainText
            elide: Text.ElideRight
            ToolTip.visible: authorHover.hovered
            ToolTip.delay: 500
            ToolTip.text: `${delegate.author} <${delegate.authorEmail}>`

            HoverHandler {
                id: authorHover
            }
        }
        Label {
            visible: delegate.columns.showDate
            Layout.preferredWidth: delegate.columns.dateWidth
            leftPadding: 6
            text: delegate.date
            elide: Text.ElideRight
        }
        Label {
            visible: delegate.columns.showSha
            Layout.preferredWidth: delegate.columns.shaWidth
            leftPadding: 6
            text: delegate.shortSha
            font.family: AppSettings.fonts.codeFamily
            elide: Text.ElideRight
        }
    }

    TapHandler {
        onTapped: delegate.clicked()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => delegate.contextMenuRequested(eventPoint.position)
    }
}
