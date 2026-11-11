pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// Commits composed from change clusters: every hunk between HEAD and the
// working tree, staged or not, goes into a cluster of related work, and
// each cluster becomes a commit of its own. The staging area is not used;
// committing a cluster leaves the other changes where they are.
//
// How the clusters are shown and built is ClusterView's, which differs
// between the concepts tried here.
ApplicationWindow {
    id: window

    required property string workDir
    required property font monoFont

    // After each commit, so the history can be read again.
    signal committed(string sha)

    // A cluster's color, from the theme's lanes.
    function clusterColor(index: int): color {
        const lanes = Styles.TokenInterface.semantics.graph_lanes
        return lanes[index % lanes.length]
    }

    width: 1300
    height: 850
    visible: true
    title: qsTr("Compose commits — %1").arg(workDir)

    ChangeClusters {
        id: clusters
        path: window.workDir
        onCommitted: sha => window.committed(sha)
    }

    Shortcut {
        sequences: [StandardKey.Refresh]
        onActivated: clusters.reload()
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            Label {
                text: qsTr("Compose commits")
                font.bold: true
            }
            Label {
                text: qsTr("%n hunk(s), %1 not in a cluster", "", clusters.hunks.length).arg(clusters.unassignedCount)
                color: Styles.TokenInterface.semantics.text_muted
            }
            Item {
                Layout.fillWidth: true
            }
            IconButton {
                iconName: "refresh-cw"
                text: qsTr("Reload")
                onClicked: clusters.reload()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Read the working tree again; hunks keep their clusters")
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ClusterView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clusters: clusters
            monoFont: window.monoFont
            colorOf: window.clusterColor
        }

        Pane {
            Layout.fillWidth: true
            visible: clusters.error !== ""
            padding: 8
            background: Rectangle {
                color: Qt.alpha(Styles.TokenInterface.semantics.danger_default, 0.15)
            }
            Label {
                anchors.fill: parent
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: clusters.error
                color: Styles.TokenInterface.semantics.danger_default
            }
        }
    }
}
