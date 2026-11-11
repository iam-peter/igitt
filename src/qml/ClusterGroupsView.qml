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
    // The newest commit and the groups share the board, each column
    // between these widths; past the narrowest, the board scrolls, as it
    // does to the older commits
    readonly property int newGroupWidth: 200
    readonly property int filesWidth: 380
    readonly property int columnWidth: {
        const count = clusters.clusters.length + (clusters.commits.length > 0 ? 1 : 0)
        if (count === 0)
            return 320
        // From the view, not the board, whose size follows its columns
        const free = width - filesWidth - 24 - newGroupWidth - 8 * count - 10
        return Math.max(320, Math.min(600, Math.floor(free / count)))
    }
    // The board's columns side by side, known before they are laid out
    readonly property real boardWidth: (clusters.commits.length + clusters.clusters.length) * (columnWidth + 8)
                                       + (clusters.commits.length > 0 ? 10 : 0) + newGroupWidth
    // How many commits the board showed, and the column it shows first,
    // to keep its place as commits come and go and columns change width
    property int shownCommits: -1
    property real firstColumn: 0
    property bool placing: false
    // The change being dragged, and where the pointer is
    property var dragged: null
    property point dragPoint
    // A group being moved by its grip: its id and where it would go
    property int movedGroup: -1
    property int moveTarget: -1

    function hunkById(id: string): var {
        return clusters.hunks.find(hunk => hunk.id === id) ?? null
    }
    function dropOn(clusterId: int) {
        if (dragged)
            composer.place(dragged, clusterId)
    }
    // Where a group lands for a pointer over the board's row
    function groupIndexAt(x: real): int {
        return Math.max(0, Math.min(clusters.clusters.length - 1,
                                    Math.floor((x + 4) / (columnWidth + 8))))
    }

    // What every change that can be dragged has: the pointer picks it up,
    // a ghost follows it, and it lands where the ghost is let go. Not while
    // a dialog or menu is open (ApplicationWindow.popupOpen).
    component Draggable: DragHandler {
        required property var hunk

        target: null
        // A card split open picks and carries its lines itself
        enabled: !parent.splitting && !(parent.Window.window?.popupOpen ?? false)
        // A list does not take the drag over to scroll
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        onActiveChanged: {
            if (active) {
                view.dragged = hunk
            } else {
                ghost.Drag.drop()
                view.dragged = null
            }
        }
        onCentroidChanged: {
            if (active)
                view.dragPoint = parent.mapToItem(view, centroid.position)
        }
    }

    // A small label in a color, such as HEAD on the newest commit
    component Tag: Rectangle {
        property alias text: tagLabel.text

        implicitWidth: tagLabel.implicitWidth + 10
        implicitHeight: tagLabel.implicitHeight + 2
        radius: 3
        Label {
            id: tagLabel
            anchors.centerIn: parent
            color: Styles.TokenInterface.textOn(parent.color)
            font.pixelSize: 10
            font.bold: true
        }
    }

    // The board keeps its place, in columns: at first the newest commit is
    // the first column shown, a commit made from a group moves the board
    // along by its column, and columns changing width keep the first one
    function followHistory() {
        const count = clusters.commits.length
        const before = shownCommits
        if (before === count)
            return
        shownCommits = count
        firstColumn = before < 0 ? Math.max(0, count - 1) : Math.max(0, firstColumn + count - before)
        placeBoard()
    }
    function placeBoard() {
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
        function onChanged() {
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

        hunk: modelData
        monoFont: view.monoFont
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
            cursorShape: Qt.OpenHandCursor
        }
        Draggable {
            hunk: card.modelData
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
                        hunks: file.hunks.map(id => view.hunkById(id)).filter(hunk => hunk && hunk.cluster < 0)
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

        // The commits not merged yet, oldest first, then the groups, side by
        // side in the order they are committed, and a place for a new one:
        // the history and what follows it, scrolled sideways
        ScrollView {
            id: board

            // What is left, whatever the columns need
            Layout.preferredWidth: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: view.boardWidth
            contentHeight: availableHeight
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

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
                            readonly property bool isHead: index === view.clusters.commits.length - 1
                            readonly property bool target: commitDrop.containsDrag && view.dragged?.commit !== modelData.sha

                            objectName: "commitColumn"
                            width: view.columnWidth
                            height: parent.height
                            padding: 0
                            background: Rectangle {
                                radius: 6
                                color: commit.target ? Qt.alpha(view.s.primary_default, 0.08) : view.s.background_default
                                border.width: commit.target ? 2 : 1
                                border.color: commit.target ? view.s.primary_default : view.s.stroke_subtle
                            }

                            DropArea {
                                id: commitDrop
                                anchors.fill: parent
                                keys: ["hunk"]
                                onDropped: {
                                    if (view.dragged)
                                        view.composer.placeInCommit(view.dragged, commit.modelData.sha)
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
                                        color: view.s.primary_default
                                    }
                                    Tag {
                                        visible: commit.modelData.published
                                        text: qsTr("pushed")
                                        color: view.s.warning_default
                                        ToolTip.visible: pushedHover.hovered
                                        ToolTip.text: qsTr("A remote branch has it: changing it means pushing again")
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
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 6
                                    model: commit.modelData.pieces
                                    delegate: DraggableCard {
                                        width: ListView.view.width
                                        maxLines: 2
                                    }
                                    ScrollBar.vertical: ScrollBar {}
                                }
                            }
                        }
                    }
                }

                // Where the history ends and what is still to commit begins
                Rectangle {
                    visible: view.clusters.commits.length > 0
                    width: 2
                    height: parent.height
                    radius: 1
                    color: view.s.stroke_field
                }

                Row {
                    id: groups

                    height: parent.height
                    spacing: 8

                    Repeater {
                        model: view.clusters.clusters
                        delegate: Frame {
                            id: group

                            objectName: "groupColumn"
                            required property var modelData
                            required property int index
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
                                visible: view.movedGroup >= 0 && view.moveTarget === group.index
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
                                                        parent.mapToItem(groups, centroid.position).x)
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
                                            text: group.index + 1
                                            color: Styles.TokenInterface.textOn(group.tint)
                                            font.pixelSize: 11
                                            font.bold: true
                                        }
                                        ToolTip.visible: orderHover.hovered
                                        ToolTip.text: qsTr("Committed as number %1").arg(group.index + 1)
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
                                ListView {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 6
                                    model: group.modelData.hunks.map(id => view.hunkById(id)).filter(hunk => hunk)
                                    delegate: DraggableCard {
                                        width: ListView.view.width
                                        maxLines: 2
                                        tint: group.tint
                                    }
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
