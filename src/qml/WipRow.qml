pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core
import IgittStyle as Styles

// Uncommitted changes, on top of the graph and connected to HEAD.
Control {
    id: wip

    required property Repository repo

    readonly property var columns: AppSettings.graph
    readonly property bool current: repo.diffSource === Repository.UnstagedDiff
                                    || repo.diffSource === Repository.StagedDiff
    readonly property var counts: repo.wipCounts

    visible: repo.hasWip
    implicitHeight: repo.hasWip ? 30 : 0
    padding: 0
    hoverEnabled: true

    background: Rectangle {
        color: wip.current ? Qt.alpha(wip.repo.wipLaneColor, 0.3)
             : wip.hovered ? Qt.alpha(wip.repo.wipLaneColor, 0.12)
             : wip.palette.base
    }

    contentItem: RowLayout {
        spacing: 0

        Item {
            visible: wip.columns.showRefs
            Layout.preferredWidth: wip.columns.refsWidth
        }
        CommitGraph {
            visible: wip.columns.showGraph
            Layout.preferredWidth: wip.columns.graphWidth
            Layout.fillHeight: true
            clip: true
            row: wip.repo.wipGraph
            nodeKind: CommitGraph.WorkingTree
            outlineColor: wip.palette.base
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 6
            spacing: 12

            Label {
                text: qsTr("// WIP")
                font.italic: true
                font.bold: true
                opacity: 0.8
            }
            Label {
                visible: wip.counts.modified > 0
                text: `✎ ${wip.counts.modified}`
                color: Styles.TokenInterface.semantics.warning_default
                ToolTip.visible: modifiedHover.hovered
                ToolTip.text: qsTr("%n modified file(s)", "", wip.counts.modified)
                HoverHandler {
                    id: modifiedHover
                }
            }
            Label {
                visible: wip.counts.added > 0
                text: `+ ${wip.counts.added}`
                color: Styles.TokenInterface.semantics.success_default
                ToolTip.visible: addedHover.hovered
                ToolTip.text: qsTr("%n new file(s)", "", wip.counts.added)
                HoverHandler {
                    id: addedHover
                }
            }
            Label {
                visible: wip.counts.deleted > 0
                text: `− ${wip.counts.deleted}`
                color: Styles.TokenInterface.semantics.danger_default
                ToolTip.visible: deletedHover.hovered
                ToolTip.text: qsTr("%n deleted file(s)", "", wip.counts.deleted)
                HoverHandler {
                    id: deletedHover
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }
    }

    TapHandler {
        onTapped: wip.repo.showWorkingTree()
    }
}
