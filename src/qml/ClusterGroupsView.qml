pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// Commits composed by drag and drop: the changes not in a group yet, by
// file, on the left; on the right the groups of work, in the order they
// are committed. A change is dragged onto a group to go into it, onto
// another group to move, and back onto the files to leave its group; a
// change is in one place only, so what is in a group is gone from the
// left. The scissors open a change, so some of its lines can be picked and
// dragged the same way. A group is moved by its grip, and committed on its
// own with its whole message.
//
// Left of the groups, scrolled to, are the commits not merged yet, the
// newest next to the groups: changes dropped onto one are folded into it,
// and its changes can be dragged onto a group, back to the files or onto
// another commit (composer.place(), placeInCommit()).
Item {
    id: view

    required property ChangeClusters clusters
    // The window: commitCluster(), commitAll(), editMessage(), splitHunk(),
    // splitPiece, place(), placeInCommit(), busy
    required property var composer
    required property font monoFont
    required property var colorOf

    readonly property Styles.Semantics s: Styles.TokenInterface.semantics
    // The clusters, read once per change: each read of the property makes
    // the list anew, with every piece of every group
    readonly property var groupList: clusters.clusters
    // The newest commit and the groups share the board, each column
    // between these widths; past the narrowest, the board scrolls, as it
    // does to the older commits
    readonly property int newGroupWidth: 200
    readonly property int filesWidth: 380
    readonly property int columnWidth: {
        const count = groupList.length + (clusters.commitCount > 0 ? 1 : 0)
        if (count === 0)
            return 320
        // From the view, not the board, whose size follows its columns
        const free = width - filesWidth - 24 - newGroupWidth - 8 * count - 10
        return Math.max(320, Math.min(600, Math.floor(free / count)))
    }
    // The board's columns side by side, known before they are laid out
    readonly property int pinnedCount: groupList.filter(cluster => cluster.pinned).length
    readonly property real boardWidth: (clusters.commitCount + groupList.length - pinnedCount)
                                       * (columnWidth + 8) + (clusters.commitCount > 0 ? 10 : 0) + newGroupWidth
    // How many commits the board showed, and the column it shows first,
    // to keep its place as commits come and go and columns change width
    property int shownCommits: -1
    property real firstColumn: 0
    property bool placing: false
    // The change being dragged, and where the pointer is
    property var dragged: null
    // A locked commit whose change is held: it shows why it stays
    property string refusedCommit
    property point dragPoint
    // A group being moved by its grip: its id and where it would go
    property int movedGroup: -1
    property int moveTarget: -1

    // Whether a commit is one a remote branch has: changing it would need
    // a force push, so nothing goes in or out of it
    function isLocked(sha: string): bool {
        return sha !== "" && clusters.isPublished(sha)
    }
    function dropOn(clusterId: int) {
        if (dragged)
            composer.place(dragged, clusterId)
    }
    // Where a group lands in the order, for a pointer over the row of the
    // groups pinned or not: at the place of the group there
    function groupIndexAt(x: real, pinned: bool): int {
        const row = groupList.filter(cluster => cluster.pinned === pinned)
        if (row.length === 0)
            return -1
        const slot = Math.max(0, Math.min(row.length - 1, Math.floor((x + 4) / (columnWidth + 8))))
        return groupList.findIndex(cluster => cluster.id === row[slot].id)
    }

    // What every change that can be dragged has: the pointer picks it up,
    // a ghost follows it, and it lands where the ghost is let go. Not while
    // a dialog or menu is open (ApplicationWindow.popupOpen).
    component Draggable: DragHandler {
        required property var hunk
        // Of a commit a remote branch has: held, it only shows that it
        // stays where it is
        readonly property bool locked: view.isLocked(hunk.commit ?? "")

        target: null
        // A card split open picks and carries its lines itself
        enabled: !parent.splitting && !(parent.Window.window?.popupOpen ?? false)
        // A list does not take the drag over to scroll
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        onActiveChanged: {
            if (locked) {
                view.refusedCommit = active ? hunk.commit : ""
            } else if (active) {
                view.dragged = hunk
            } else {
                ghost.Drag.drop()
                view.dragged = null
            }
        }
        onCentroidChanged: {
            if (active && !locked)
                view.dragPoint = parent.mapToItem(view, centroid.position)
        }
    }

    // The board keeps its place, in columns: at first the newest commit is
    // the first column shown, a commit made from a group moves the board
    // along by its column, and columns changing width keep the first one
    function followHistory() {
        const count = clusters.commitCount
        const before = shownCommits
        if (before === count)
            return
        shownCommits = count
        firstColumn = before < 0 ? Math.max(0, count - 1) : Math.max(0, firstColumn + count - before)
        placeBoard()
    }
    // Until the board is placed, what moves it is not the user: the board
    // keeping its bounds as it narrows, say
    // The board moved sideways by a wheel's turn
    function scrollBoard(delta: real) {
        const flick = board.contentItem
        flick.contentX = Math.max(0, Math.min(Math.max(0, boardWidth - flick.width), flick.contentX - delta))
    }
    function placeBoard() {
        placing = true
        Qt.callLater(() => {
            const flick = board.contentItem
            placing = true
            flick.contentX = Math.max(0, Math.min(Math.max(0, boardWidth - flick.width),
                                                  firstColumn * (columnWidth + 8)))
            placing = false
        })
    }
    onColumnWidthChanged: placeBoard()
    onBoardWidthChanged: placeBoard()
    Connections {
        target: view.clusters
        function onCommitsChanged() {
            view.followHistory()
        }
    }
    Connections {
        target: board.contentItem
        function onContentXChanged() {
            if (!view.placing)
                view.firstColumn = board.contentItem.contentX / (view.columnWidth + 8)
        }
    }
    Component.onCompleted: followHistory()

    // A change on a card: dragged whole, or split open to carry some of
    // its lines out the same way
    component DraggableCard: HunkCard {
        id: card

        required property var modelData
        readonly property bool locked: view.isLocked(modelData.commit ?? "")

        hunk: modelData
        monoFont: view.monoFont
        splittable: !locked && (modelData.lines?.length ?? 0) > 1
        splitting: view.composer.splitPiece === modelData.id
        opacity: view.dragged?.id === modelData.id && !view.dragged.pick ? 0.4 : 1
        onSplitRequested: view.composer.splitHunk(modelData.id)
        onCarryStarted: piece => view.dragged = piece
        onCarryMoved: position => view.dragPoint = card.mapToItem(view, position)
        onCarryEnded: {
            ghost.Drag.drop()
            view.dragged = null
        }
        HoverHandler {
            enabled: !card.splitting
            cursorShape: card.locked ? Qt.ForbiddenCursor : Qt.OpenHandCursor
        }
        Draggable {
            hunk: card.modelData
        }
    }

    // A group: its order, title, message, changes and commit button. On
    // the board, or pinned at its side, where it stays in sight as the
    // board scrolls into the history; lane is the row it stands in.
    component GroupColumn: Frame {
        id: group

        objectName: "groupColumn"
        required property var modelData
        required property Item lane
        // Its place in the order the groups are committed, pinned or not
        readonly property int order: view.groupList.findIndex(cluster => cluster.id === modelData.id)
        readonly property color tint: view.colorOf(modelData.color)

        width: view.columnWidth
        height: parent.height
        padding: 0
        opacity: view.movedGroup === modelData.id ? 0.5 : 1
        background: Rectangle {
            radius: 6
            color: drop.containsDrag ? Qt.alpha(group.tint, 0.12) : view.s.background_muted
            border.width: drop.containsDrag ? 2 : 1
            border.color: drop.containsDrag ? group.tint : view.s.stroke_subtle
        }

        // Where a moved group would go
        Rectangle {
            visible: view.movedGroup >= 0 && view.moveTarget === group.order
                     && view.movedGroup !== group.modelData.id
            x: -6
            width: 3
            height: parent.height
            radius: 1.5
            color: view.s.primary_default
        }

        DropArea {
            id: drop
            anchors.fill: parent
            keys: ["hunk"]
            onDropped: view.dropOn(group.modelData.id)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                // The grip moves the group in the order. Its
                // handlers are on an item of their own: Icon is
                // a disabled button, which takes no events.
                Item {
                    implicitWidth: 16
                    implicitHeight: 20
                    Icon {
                        anchors.centerIn: parent
                        name: "grip-vertical"
                        size: 14
                        color: view.s.text_muted
                    }
                    HoverHandler {
                        cursorShape: Qt.SizeHorCursor
                    }
                    DragHandler {
                        target: null
                        enabled: !(parent.Window.window?.popupOpen ?? false)
                        onActiveChanged: {
                            if (active) {
                                view.movedGroup = group.modelData.id
                            } else {
                                if (view.moveTarget >= 0)
                                    view.clusters.moveCluster(view.movedGroup, view.moveTarget)
                                view.movedGroup = -1
                                view.moveTarget = -1
                            }
                        }
                        onCentroidChanged: {
                            if (active)
                                view.moveTarget = view.groupIndexAt(
                                    parent.mapToItem(group.lane, centroid.position).x, group.modelData.pinned)
                        }
                    }
                }
                Rectangle {
                    implicitWidth: 20
                    implicitHeight: 20
                    radius: 10
                    color: group.tint
                    Label {
                        anchors.centerIn: parent
                        text: group.order + 1
                        color: Styles.TokenInterface.textOn(group.tint)
                        font.pixelSize: 11
                        font.bold: true
                    }
                    ToolTip.visible: orderHover.hovered
                    ToolTip.text: qsTr("Committed as number %1").arg(group.order + 1)
                    HoverHandler {
                        id: orderHover
                    }
                }
                TextField {
                    Layout.fillWidth: true
                    styles: [Styles.FieldStyle.small]
                    text: group.modelData.name
                    selectByMouse: true
                    // The start of the title, not its end
                    Component.onCompleted: cursorPosition = 0
                    onEditingFinished: {
                        view.clusters.renameCluster(group.modelData.id, text)
                        cursorPosition = 0
                    }
                }
                IconButton {
                    objectName: "pinButton"
                    iconName: "pin"
                    checkable: true
                    checked: group.modelData.pinned
                    // Pinned, in the group's color
                    icon.color: checked ? group.tint : palette.buttonText
                    onClicked: view.clusters.setClusterPinned(group.modelData.id, checked)
                    ToolTip.visible: hovered
                    ToolTip.text: group.modelData.pinned ? qsTr("Unpin: back onto the board")
                                                         : qsTr("Pin at the side, in sight while the board scrolls")
                }
                IconButton {
                    iconName: "message-square"
                    onClicked: view.composer.editMessage(group.modelData.id)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Write the whole message")
                }
                IconButton {
                    iconName: "trash-2"
                    onClicked: view.clusters.removeCluster(group.modelData.id)
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Remove the group; its changes go back to the left")
                }
            }
            Label {
                text: group.modelData.hunks.length === 0
                      ? qsTr("Drag changes here")
                      : qsTr("%n change(s) in %1 file(s)", "", group.modelData.hunks.length)
                            .arg(group.modelData.files.length)
                      + `  +${group.modelData.added} −${group.modelData.removed}`
                color: view.s.text_muted
                font.pixelSize: 11
            }
            // Into the column's padding on the right, where its bar sits
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.rightMargin: -8
                clip: true
                spacing: 6
                model: group.modelData.pieces
                delegate: DraggableCard {
                    width: ListView.view.width - 8
                    maxLines: 2
                    tint: group.tint
                }
                ScrollBar.vertical: ScrollBar {}
            }
            Button {
                Layout.fillWidth: true
                styles: [Styles.ButtonStyle.small]
                highlighted: true
                icon.source: Icons.source("git-commit-horizontal")
                icon.width: 16
                icon.height: 16
                text: qsTr("Commit “%1”").arg(group.modelData.name)
                enabled: group.modelData.hunks.length > 0 && !view.composer.committing
                onClicked: view.composer.commitCluster(group.modelData.id)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // The changes not in a group, by file
        Frame {
            Layout.preferredWidth: view.filesWidth
            Layout.fillHeight: true
            padding: 0

            DropArea {
                id: filesDrop
                anchors.fill: parent
                keys: ["hunk"]
                onDropped: view.dropOn(-1)

                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: Qt.alpha(view.s.primary_default, 0.08)
                    visible: filesDrop.containsDrag && (view.dragged?.cluster >= 0 || (view.dragged?.commit ?? "") !== "")
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    padding: 8
                    text: qsTr("Not in a group (%1)").arg(view.clusters.unassignedCount)
                    font.bold: true
                }
                ListView {
                    id: files

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 10
                    // The same on both sides; the bar sits in the right one
                    leftMargin: 8
                    rightMargin: 8
                    bottomMargin: 8
                    // Only files with changes left to place
                    model: view.clusters.files.map(file => ({
                        path: file.path,
                        hunks: file.pieces.filter(piece => piece.cluster < 0)
                    })).filter(file => file.hunks.length > 0)
                    delegate: ColumnLayout {
                        id: file

                        required property var modelData

                        width: ListView.view.width - 16
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Icon {
                                name: "file"
                                size: 14
                            }
                            Label {
                                Layout.fillWidth: true
                                text: file.modelData.path
                                textFormat: Text.PlainText
                                elide: Text.ElideMiddle
                                font.bold: true
                            }
                        }
                        Repeater {
                            model: file.modelData.hunks
                            delegate: DraggableCard {
                                Layout.fillWidth: true
                                showPath: false
                                maxLines: 3
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: files.count === 0
                        text: qsTr("Every change is in a group")
                        color: view.s.text_muted
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }
        }

        // The timeline of the commits above the board, which scrolls to where
        // it is clicked; what is shown of them is its band
        ColumnLayout {
            // What is left, whatever the columns need
            Layout.preferredWidth: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            ActivityMinimap {
                id: timeline

                objectName: "composeTimeline"
                // The commits shown, by the board's place: the newest is row 0
                readonly property real step: view.columnWidth + 8
                readonly property real fromColumn: board.contentItem.contentX / step
                readonly property real toColumn: fromColumn + board.width / step - 0.01

                Layout.fillWidth: true
                Layout.preferredHeight: 54
                visible: view.clusters.commitCount > 0
                times: view.clusters.commitTimes
                currentRow: -1
                firstVisibleRow: fromColumn >= view.clusters.commitCount ? -1
                               : view.clusters.commitCount - 1 - Math.min(view.clusters.commitCount - 1, Math.floor(toColumn))
                lastVisibleRow: fromColumn >= view.clusters.commitCount ? -1
                              : view.clusters.commitCount - 1 - Math.floor(fromColumn)
                lineColor: view.palette.highlight
                bandColor: view.palette.highlight
                textColor: view.palette.placeholderText
                onRowRequested: row => {
                    view.firstColumn = view.clusters.commitCount - 1 - row
                    view.placeBoard()
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // The commits not merged yet, oldest first, then the groups, side by
                // side in the order they are committed, and a place for a new one:
                // the history and what follows it, scrolled sideways
                ScrollView {
                    id: board

                    anchors.fill: parent
                    contentWidth: view.boardWidth
                    contentHeight: availableHeight
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                    // In sight whenever there is more than fits, the bar beneath the
                    // columns rather than over them
                    ScrollBar.horizontal.policy: view.boardWidth > width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    bottomPadding: ScrollBar.horizontal.policy === ScrollBar.AlwaysOn ? ScrollBar.horizontal.height + 2 : 0

                    Row {
                        height: board.availableHeight
                        spacing: 8

                        Row {
                            id: history

                            height: parent.height
                            spacing: 8

                            Repeater {
                                model: view.clusters.commits
                                delegate: Frame {
                                    id: commit

                                    required property var modelData
                                    required property int index
                                    readonly property bool isHead: index === view.clusters.commitCount - 1
                                    // In sight, or a column away: only then are its cards made
                                    readonly property bool inSight: x + width >= board.contentItem.contentX - view.columnWidth
                                                                    && x <= board.contentItem.contentX + board.width + view.columnWidth
                                    readonly property bool locked: modelData.published
                                    readonly property bool dropping: commitDrop.containsDrag && view.dragged?.commit !== modelData.sha
                                    readonly property bool target: dropping && !locked
                                    // Something would go in or out, and cannot
                                    readonly property bool refused: locked && (dropping || view.refusedCommit === modelData.sha)

                                    objectName: "commitColumn"
                                    width: view.columnWidth
                                    height: parent.height
                                    padding: 0
                                    background: Rectangle {
                                        radius: 6
                                        color: commit.refused ? Qt.alpha(view.s.danger_default, 0.08)
                                             : commit.target ? Qt.alpha(view.s.primary_default, 0.08) : view.s.background_default
                                        border.width: commit.refused ? 4 : commit.target ? 2 : 1
                                        border.color: commit.refused ? view.s.danger_default
                                                    : commit.target ? view.s.primary_default : view.s.stroke_subtle
                                    }

                                    DropArea {
                                        id: commitDrop
                                        anchors.fill: parent
                                        keys: ["hunk"]
                                        onDropped: {
                                            if (view.dragged && !commit.locked)
                                                view.composer.placeInCommit(view.dragged, commit.modelData.sha)
                                        }
                                    }
                                    // Why nothing goes in or out
                                    Rectangle {
                                        objectName: "refusedNote"
                                        visible: commit.refused
                                        z: 1
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 8
                                        height: refusedText.implicitHeight + 12
                                        radius: 4
                                        color: view.s.danger_default
                                        Label {
                                            id: refusedText
                                            anchors.centerIn: parent
                                            width: parent.width - 16
                                            horizontalAlignment: Text.AlignHCenter
                                            wrapMode: Text.Wrap
                                            text: qsTr("Pushed: a remote branch has this commit, and changing it would need a force push")
                                            color: Styles.TokenInterface.textOn(view.s.danger_default)
                                            font.bold: true
                                        }
                                    }

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 6

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6

                                            Icon {
                                                name: "git-commit-horizontal"
                                                size: 16
                                                color: view.s.text_muted
                                            }
                                            Label {
                                                text: commit.modelData.shortSha
                                                color: view.s.text_muted
                                                font.family: view.monoFont.family
                                                font.pixelSize: 11
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                text: commit.modelData.subject
                                                textFormat: Text.PlainText
                                                elide: Text.ElideRight
                                                font.bold: true
                                                ToolTip.visible: subjectHover.hovered && truncated
                                                ToolTip.text: text
                                                HoverHandler {
                                                    id: subjectHover
                                                }
                                            }
                                            Tag {
                                                visible: commit.isHead
                                                text: qsTr("HEAD")
                                                tint: view.s.primary_default
                                                filled: true
                                            }
                                            Tag {
                                                objectName: "pushedTag"
                                                visible: commit.modelData.published
                                                text: qsTr("pushed")
                                                tint: view.s.warning_default
                                                filled: true
                                                ToolTip.visible: pushedHover.hovered
                                                ToolTip.text: qsTr("A remote branch has it, so it stays as it is: changing it would need a force push")
                                                HoverHandler {
                                                    id: pushedHover
                                                }
                                            }
                                        }
                                        Label {
                                            text: qsTr("%n change(s) in %1 file(s)", "", commit.modelData.pieces.length)
                                                      .arg(commit.modelData.files.length)
                                                  + `  +${commit.modelData.added} −${commit.modelData.removed}`
                                            color: view.s.text_muted
                                            font.pixelSize: 11
                                        }
                                        // Into the column's padding on the right, where its bar sits
                                        Loader {
                                            objectName: "commitCards"
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            Layout.rightMargin: -8
                                            active: commit.inSight
                                            sourceComponent: ListView {
                                                clip: true
                                                spacing: 6
                                                model: commit.modelData.pieces
                                                delegate: DraggableCard {
                                                    width: ListView.view.width - 8
                                                    maxLines: 2
                                                }
                                                ScrollBar.vertical: ScrollBar {}
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Where the history ends and what is still to commit begins
                        Rectangle {
                            visible: view.clusters.commitCount > 0
                            width: 2
                            height: parent.height
                            radius: 1
                            color: view.s.stroke_field
                        }

                        Row {
                            id: groups

                            height: parent.height
                            spacing: 8

                            // Only those not pinned: the dock has the others
                            Repeater {
                                model: view.groupList.filter(cluster => !cluster.pinned)
                                delegate: GroupColumn {
                                    lane: groups
                                }
                            }

                            // Dropping here makes a group of the change
                            Rectangle {
                                objectName: "newGroupDrop"
                                width: view.newGroupWidth
                                height: parent.height
                                radius: 6
                                color: newDrop.containsDrag ? Qt.alpha(view.s.primary_default, 0.08) : Styles.TokenInterface.transparent
                                border.width: 1
                                border.color: newDrop.containsDrag ? view.s.primary_default : view.s.stroke_field

                                DropArea {
                                    id: newDrop
                                    anchors.fill: parent
                                    keys: ["hunk"]
                                    onDropped: {
                                        const piece = view.dragged
                                        view.composer.place(piece, view.clusters.addCluster(""))
                                    }
                                }
                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    Button {
                                        Layout.alignment: Qt.AlignHCenter
                                        styles: [Styles.ButtonStyle.small]
                                        icon.source: Icons.source("plus")
                                        icon.width: 16
                                        icon.height: 16
                                        text: qsTr("New group")
                                        onClicked: view.clusters.addCluster("")
                                    }
                                    Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: qsTr("or drop a change here")
                                        color: view.s.text_muted
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }

                // Shift and the wheel scroll sideways, as Alt and the wheel do
                // in Qt but desktops often take; over the board, before the
                // lists in it scroll down
                Item {
                    anchors.fill: parent
                    WheelHandler {
                        target: null
                        acceptedModifiers: Qt.ShiftModifier
                        onWheel: event => view.scrollBoard(event.angleDelta.y !== 0 ? event.angleDelta.y
                                                                                    : event.angleDelta.x)
                    }
                }
            }
        }

        // The groups pinned, beside the board, in their order
        Row {
            id: dock

            Layout.fillHeight: true
            visible: view.pinnedCount > 0
            spacing: 8

            Repeater {
                model: view.groupList.filter(cluster => cluster.pinned)
                delegate: GroupColumn {
                    lane: dock
                }
            }
        }
    }

    // What follows the pointer while a change is dragged
    HunkCard {
        id: ghost

        visible: view.dragged !== null
        hunk: view.dragged ?? {path: "", line: 0, added: 0, removed: 0, section: "", lines: []}
        monoFont: view.monoFont
        splittable: false
        width: 260
        maxLines: 1
        x: view.dragPoint.x - 20
        y: view.dragPoint.y - 12
        z: 10
        opacity: 0.9
        Drag.active: view.dragged !== null
        Drag.keys: ["hunk"]
        Drag.hotSpot.x: 20
        Drag.hotSpot.y: 12
    }
}
