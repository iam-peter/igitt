pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// Commits composed from change clusters: every changed line between HEAD
// and the working tree, staged or not, goes into a cluster of related
// work, and each cluster becomes a commit of its own, with a message
// written as in the commit form. The staging area is not used; committing
// a cluster leaves the other changes where they are. While planning, the
// commits go into the plan, and the changes are read against it.
//
// How the clusters are shown and built is ClusterView's, which differs
// between the concepts tried here. It calls back here (composer) to commit
// and to open the dialogs.
ApplicationWindow {
    id: window

    required property Repository repo
    required property font monoFont
    // The main window's Plan button: starts planning, or ends it (asking
    // first when the plan has entries)
    required property var setPlanning

    // The clusters still to commit, in order, and the one on its way
    property var queue: []
    property int pending: -1
    readonly property bool committing: pending >= 0

    // A cluster's color, from the theme's lanes.
    function clusterColor(index: int): color {
        const lanes = Styles.TokenInterface.semantics.graph_lanes
        return lanes[index % lanes.length]
    }
    function editMessage(clusterId: int) {
        messageDialog.edit(clusterId)
    }
    function splitHunk(hunkId: string) {
        linesDialog.edit(hunkId)
    }
    // A commit of the cluster, through the repository: right away, or
    // planned while planning
    function commitCluster(clusterId: int) {
        queue = [clusterId]
        next()
    }
    // Every cluster that has changes, in its order
    function commitAll() {
        queue = clusters.clusters.filter(cluster => cluster.hunks.length > 0).map(cluster => cluster.id)
        next()
    }
    function next() {
        if (queue.length === 0) {
            pending = -1
            return
        }
        pending = queue[0]
        queue = queue.slice(1)
        const patch = clusters.clusterPatch(pending)
        if (patch.byteLength === 0) {
            next()
            return
        }
        clusters.setError("")
        repo.commitPatch(patch, clusters.clusterMessage(pending))
    }

    width: 1300
    height: 850
    visible: true
    title: qsTr("Compose commits — %1").arg(repo.path)

    ChangeClusters {
        id: clusters
        path: window.repo.path
        // The planned HEAD while planning
        base: window.repo.planning && window.repo.headSha !== "" ? window.repo.headSha : "HEAD"
    }

    Connections {
        target: window.repo
        function onPatchCommitted(ok: bool, error: string) {
            if (!window.committing)
                return
            if (!ok) {
                clusters.setError(error)
                window.queue = []
                window.pending = -1
                return
            }
            clusters.committed(window.pending)
            window.next()
        }
        // Planning began or ended: the changes are read against the other
        // HEAD
        function onPlanningChanged() {
            clusters.reload()
        }
    }

    ClusterMessageDialog {
        id: messageDialog
        clusters: clusters
        monoFont: window.monoFont
        anchors.centerIn: parent
    }
    HunkLinesDialog {
        id: linesDialog
        clusters: clusters
        monoFont: window.monoFont
        colorOf: window.clusterColor
        anchors.centerIn: parent
    }

    Shortcut {
        sequences: [StandardKey.Refresh]
        enabled: !window.popupOpen
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
            BusyIndicator {
                implicitWidth: 20
                implicitHeight: 20
                padding: 0
                running: window.committing
                visible: running
            }
            // Plan mode is the repository's, the same as in the main window
            IconButton {
                objectName: "composePlanButton"
                iconName: "list-checks"
                text: window.repo.planning ? qsTr("Planning (%n)", "", window.repo.plan.count) : qsTr("Plan")
                checkable: true
                checked: window.repo.planning
                enabled: !window.repo.writing && !window.committing
                onClicked: window.setPlanning(checked)
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Collect the commits in a plan, review the commands, and execute them together")
            }
            IconButton {
                visible: window.repo.planning
                iconName: "play"
                text: qsTr("Execute plan")
                enabled: window.repo.plan.count > 0 && !window.repo.writing && !window.committing
                onClicked: window.repo.executePlan()
            }
            IconButton {
                iconName: "refresh-cw"
                text: qsTr("Reload")
                onClicked: clusters.reload()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Read the working tree again; lines keep their clusters")
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // What committing does while planning
        Pane {
            Layout.fillWidth: true
            visible: window.repo.planning
            padding: 6
            background: Rectangle {
                color: Qt.alpha(Styles.TokenInterface.semantics.special_default, 0.2)
            }
            Label {
                anchors.fill: parent
                wrapMode: Text.Wrap
                text: qsTr("Planning: each commit goes into the plan; Execute plan makes them all at once.")
            }
        }

        ClusterView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clusters: clusters
            composer: window
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
