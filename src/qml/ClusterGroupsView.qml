pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// Concept A, drag and drop: the changes not in a group yet, by file, on
// the left; the groups of work on the right, in the order they are
// committed. A change is dragged onto a group to go into it, onto another
// group to move, and back onto the files to leave its group; a change is
// in one place only, so what is in a group is gone from the left. The
// scissors put some lines of a change into another group. A group is
// moved by its grip, and committed on its own with its whole message.
Item {
    id: view

    required property ChangeClusters clusters
    // The window: commitCluster(), commitAll(), editMessage(), splitHunk()
    required property var composer
    required property font monoFont
    required property var colorOf

    readonly property Styles.Semantics s: Styles.TokenInterface.semantics
    readonly property int columnWidth: 280
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
            clusters.assign(dragged.id, clusterId)
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
        enabled: !(parent.Window.window?.popupOpen ?? false)
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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // The changes not in a group, by file
        Frame {
            Layout.preferredWidth: 380
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
                    visible: filesDrop.containsDrag && view.dragged?.cluster >= 0
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
                        x: 8
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
                            delegate: HunkCard {
                                id: card

                                required property var modelData

                                Layout.fillWidth: true
                                hunk: modelData
                                monoFont: view.monoFont
                                showPath: false
                                maxLines: 3
                                opacity: view.dragged?.id === modelData.id ? 0.4 : 1
                                onSplitRequested: view.composer.splitHunk(modelData.hunk)
                                HoverHandler {
                                    cursorShape: Qt.OpenHandCursor
                                }
                                Draggable {
                                    hunk: card.modelData
                                }
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

        // The groups, side by side in the order they are committed, and a
        // place for a new one
        ScrollView {
            id: board

            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: availableHeight
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

            Row {
                id: groups

                height: board.availableHeight
                spacing: 8

                Repeater {
                    model: view.clusters.clusters
                    delegate: Frame {
                        id: group

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
                                delegate: HunkCard {
                                    id: member

                                    required property var modelData

                                    width: ListView.view.width
                                    hunk: modelData
                                    monoFont: view.monoFont
                                    maxLines: 2
                                    tint: group.tint
                                    opacity: view.dragged?.id === modelData.id ? 0.4 : 1
                                    onSplitRequested: view.composer.splitHunk(modelData.hunk)
                                    HoverHandler {
                                        cursorShape: Qt.OpenHandCursor
                                    }
                                    Draggable {
                                        hunk: member.modelData
                                    }
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
                    width: 220
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
                            const hunk = view.dragged
                            const id = view.clusters.addCluster("")
                            view.clusters.assign(hunk.id, id)
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
