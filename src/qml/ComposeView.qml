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
// Before the clusters come the commits of the branch not merged yet, as
// columns of their own: a change dropped onto one is folded into it, and a
// change of one can be dragged onto a cluster, out of the commit and into
// the working tree, or onto another commit. Those moves rewrite the
// history (Repository::moveChange()), or plan it; Undo takes the last one
// back.
//
// The compose window shows it, and so does the main window's Compose mode.
// While not active (out of sight) it leaves the repository's changes until
// it is again.
Item {
    id: view

    required property Repository repo
    required property font monoFont
    // The main window's Plan button: starts planning, or ends it (asking
    // first when the plan has entries)
    required property var setPlanning

    // The clusters still to commit, in order, and the one on its way
    property var queue: []
    property int pending: -1
    readonly property bool committing: pending >= 0
    // A change on its way between commits and the working tree
    property bool moving: false
    readonly property bool busy: committing || moving || repo.writing
    // The piece whose card is split open, to pick lines from; kept here,
    // as the view makes its cards anew whenever the clusters change
    property string splitPiece
    // In sight: the window is active, or the mode shown. While not, the
    // repository's changes are read once it is again.
    property bool active: true
    // Its own Plan buttons: not in the main window, which has them
    property bool showPlanButtons: true
    readonly property bool popupOpen: Window.window ? Window.window.popupOpen === true : false

    // A cluster's color, from the theme's lanes.
    function clusterColor(index: int): color {
        const lanes = Styles.TokenInterface.semantics.graph_lanes
        return lanes[index % lanes.length]
    }
    function editMessage(clusterId: int) {
        messageDialog.edit(clusterId)
    }
    // The scissors of a card: open it, or close it again
    function splitHunk(pieceId: string) {
        splitPiece = splitPiece === pieceId ? "" : pieceId
    }
    // A piece dropped onto a cluster, -1 for none: all of it, or the lines
    // picked from it (pick). A piece of a commit leaves the commit for the
    // working tree, where it goes into the cluster.
    function place(piece: var, clusterId: int) {
        if (piece.commit) {
            moveChange(piece, "", clusterId)
            return
        }
        if (piece.pick)
            clusters.assignLines(piece.hunk, piece.pick, clusterId)
        else
            clusters.assign(piece.id, clusterId)
    }
    // A piece dropped onto a commit: folded into it
    function placeInCommit(piece: var, sha: string) {
        if (piece.commit !== sha)
            moveChange(piece, sha, -1)
    }
    function moveChange(piece: var, to: string, clusterId: int) {
        if (busy)
            return
        const from = piece.commit ?? ""
        // A commit a remote branch has stays as it is, for now: changing it
        // would need a force push
        if ((from !== "" && clusters.isPublished(from)) || (to !== "" && clusters.isPublished(to))) {
            clusters.setError(qsTr("A remote branch has that commit; changing it would need a force push."))
            return
        }
        const lines = piece.pick ?? piece.lines.map(line => line.index)
        const patch = from !== "" ? clusters.commitPatch(from, piece.hunk, lines)
                                  : clusters.linesPatch(piece.hunk, lines)
        if (patch.byteLength === 0)
            return
        if (from !== "" && clusterId >= 0)
            clusters.expect(from, piece.hunk, lines, clusterId)
        const short = sha => sha.substring(0, 7)
        const title = from === "" ? qsTr("Fold %n line(s) into %1", "", lines.length).arg(short(to))
                    : to === "" ? qsTr("Take %n line(s) out of %1", "", lines.length).arg(short(from))
                    : qsTr("Move %n line(s) from %1 to %2", "", lines.length).arg(short(from)).arg(short(to))
        clusters.setError("")
        moving = true
        repo.moveChange(from, to, patch, title)
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
    // What the changes are read against: the planned HEAD while planning
    function currentBase(): string {
        return repo.planning && repo.headSha !== "" ? repo.headSha : "HEAD"
    }
    // The changes again, after they moved outside this window: a commit
    // from the main window or a terminal, a checkout, a plan begun or
    // ended. Once for all that asks in the same turn of the event loop, as
    // a move does twice, by HEAD and by its own signal.
    function followRepository() {
        if (active)
            Qt.callLater(view.readAgain)
    }
    // Not while committing, which reads them itself; lines moved out of a
    // commit go into their groups once read
    function readAgain() {
        if (committing)
            return
        const base = currentBase()
        if (clusters.base !== base)
            clusters.base = base
        else
            clusters.reload()
        if (placeMoved) {
            placeMoved = false
            clusters.placeExpected()
        }
    }
    // A move is done: its lines are to be placed after the next reading
    property bool placeMoved: false
    // A commit on its way that will not report back: the repository was
    // replaced, or the plan began or ended, and its result was dropped
    function abandonCommits(reason: string) {
        moving = false
        clusters.dropExpected()
        if (!committing)
            return
        queue = []
        pending = -1
        clusters.setError(reason)
    }

    // The base is set here, not bound: after a commit it changes together
    // with the committed cluster (committed()), in one reading
    ChangeClusters {
        id: clusters
        path: view.repo.path
        Component.onCompleted: base = view.currentBase()
    }

    onActiveChanged: {
        if (active)
            followRepository()
    }

    Connections {
        target: view.repo
        function onPatchCommitted(ok: bool, error: string) {
            if (!view.committing)
                return
            if (!ok) {
                clusters.setError(error)
                view.queue = []
                view.pending = -1
                return
            }
            clusters.committed(view.pending, view.currentBase())
            view.next()
        }
        function onChangeMoved(ok: bool, error: string) {
            if (!view.moving)
                return
            view.moving = false
            if (!ok) {
                clusters.dropExpected()
                clusters.setError(error)
            }
            view.placeMoved = ok
            view.followRepository()
        }
        function onHeadChanged() {
            view.followRepository()
        }
        function onPlanningChanged() {
            view.abandonCommits(qsTr("Planning began or ended before the commits were made; look at the history."))
            view.followRepository()
        }
        function onPathChanged() {
            view.abandonCommits("")
        }
    }

    ClusterMessageDialog {
        id: messageDialog
        clusters: clusters
        monoFont: view.monoFont
        anchors.centerIn: parent
    }
    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: view.active && view.splitPiece !== "" && !view.popupOpen
        onActivated: view.splitPiece = ""
    }
    Shortcut {
        sequences: [StandardKey.Refresh]
        enabled: view.active && !view.popupOpen
        onActivated: clusters.reload()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true
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
                    text: qsTr("%n hunk(s), %1 not in a cluster", "", clusters.pieceCount).arg(clusters.unassignedCount)
                    color: Styles.TokenInterface.semantics.text_muted
                }
                Item {
                    Layout.fillWidth: true
                }
                BusyIndicator {
                    implicitWidth: 20
                    implicitHeight: 20
                    padding: 0
                    running: view.committing || view.moving
                    visible: running
                }
                IconButton {
                    objectName: "composeUndoButton"
                    visible: view.repo.canUndoFold && !view.repo.planning
                    iconName: "step-back"
                    text: qsTr("Undo")
                    enabled: !view.busy
                    onClicked: view.repo.undoFold()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Take back the last change moved into or out of a commit")
                }
                // Plan mode is the repository's, the same as in the main window
                IconButton {
                    objectName: "composePlanButton"
                    visible: view.showPlanButtons
                    iconName: "list-checks"
                    text: view.repo.planning ? qsTr("Planning (%n)", "", view.repo.plan.count) : qsTr("Plan")
                    checkable: true
                    checked: view.repo.planning
                    enabled: !view.busy
                    onClicked: view.setPlanning(checked)
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Collect the commits in a plan, review the commands, and execute them together")
                }
                IconButton {
                    visible: view.showPlanButtons && view.repo.planning
                    iconName: "play"
                    text: qsTr("Execute plan")
                    enabled: view.repo.plan.count > 0 && !view.busy
                    onClicked: view.repo.executePlan()
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

        // What committing does while planning
        Pane {
            Layout.fillWidth: true
            visible: view.repo.planning
            padding: 6
            background: Rectangle {
                color: Qt.alpha(Styles.TokenInterface.semantics.special_default, 0.2)
            }
            Label {
                anchors.fill: parent
                wrapMode: Text.Wrap
                text: qsTr("Planning: each commit and each move goes into the plan; Execute plan makes them all at once.")
            }
        }

        ClusterGroupsView {
            objectName: "groupsView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clusters: clusters
            composer: view
            monoFont: view.monoFont
            colorOf: view.clusterColor
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
