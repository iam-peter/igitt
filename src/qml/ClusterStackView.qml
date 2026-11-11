pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// Concept D, a workspace of several future commits at once: the commits
// are planned as a stack on HEAD, newest on top as in the history, each
// with its message and its hunks, and are made together in their order.
// Hunks are dragged from the unplanned ones onto a commit, or between
// commits; a commit moves up or down the stack by its grip or the arrows.
// The scissors put some of a hunk's lines into another commit. Suggest
// makes a first stack of what is unplanned, by kind of change.
Item {
    id: view

    required property ChangeClusters clusters
    // The window: commitCluster(), commitAll(), editMessage(), splitHunk()
    required property var composer
    required property font monoFont
    required property var colorOf

    readonly property Styles.Semantics s: Styles.TokenInterface.semantics
    // The hunk being dragged, and where the pointer is
    property var dragged: null
    property point dragPoint
    // The planned commits that have hunks, the ones a stack is made of
    readonly property var planned: clusters.clusters.filter(cluster => cluster.hunks.length > 0)
    readonly property var unplanned: clusters.hunks.filter(hunk => hunk.cluster < 0)
    // Planned commits that have no changes yet, made into nothing
    readonly property int emptyPlans: clusters.clusters.length - planned.length
    // A commit moved by its grip, and the row it would go to
    property int movedCommit: -1
    property int moveTarget: -1

    function hunkById(id: string): var {
        return clusters.hunks.find(hunk => hunk.id === id) ?? null
    }
    function dropOn(clusterId: int) {
        if (dragged)
            clusters.assign(dragged.id, clusterId)
    }

    // A first stack of what is unplanned, by kind: tests, documentation,
    // then the code of each folder. Code goes first, so tests and
    // documentation come after what they are about.
    function suggest() {
        const groups = new Map()
        for (const hunk of unplanned) {
            const path = hunk.path
            const folder = path.includes("/") ? path.substring(0, path.lastIndexOf("/")) : ""
            const key = /(^|\/)tests?\//.test(path) || /(^|\/)tst_/.test(path) ? "~tests"
                      : path.endsWith(".md") || path.startsWith("docs/") ? "~docs"
                      : folder
            if (!groups.has(key))
                groups.set(key, [])
            groups.get(key).push(hunk.id)
        }
        const keys = Array.from(groups.keys()).sort()
        for (const key of keys) {
            const name = key === "~tests" ? qsTr("Test it")
                       : key === "~docs" ? qsTr("Document it")
                       : key === "" ? qsTr("Change the top level")
                       : qsTr("Change %1").arg(key.split("/").pop())
            const id = clusters.addCluster(name)
            for (const hunkId of groups.get(key))
                clusters.assign(hunkId, id)
        }
    }

    component Draggable: DragHandler {
        required property var hunk

        target: null
        // Not under a dialog or menu (ApplicationWindow.popupOpen)
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
        spacing: 12

        // What is not planned yet
        Frame {
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            padding: 0

            DropArea {
                anchors.fill: parent
                keys: ["hunk"]
                onDropped: view.dropOn(-1)
            }
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Not planned (%1)").arg(view.unplanned.length)
                        font.bold: true
                    }
                    Button {
                        styles: [Styles.ButtonStyle.small]
                        text: qsTr("Suggest")
                        enabled: view.unplanned.length > 0
                        onClicked: view.suggest()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Plan a commit for each kind of change: code by folder, tests, documentation")
                    }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    leftMargin: 8
                    rightMargin: 8
                    model: view.unplanned
                    delegate: HunkCard {
                        id: loose

                        required property var modelData

                        width: ListView.view.width - 16
                        hunk: modelData
                        monoFont: view.monoFont
                        maxLines: 2
                        opacity: view.dragged?.id === modelData.id ? 0.4 : 1
                        onSplitRequested: view.composer.splitHunk(modelData.hunk)
                        HoverHandler {
                            cursorShape: Qt.OpenHandCursor
                        }
                        Draggable {
                            hunk: loose.modelData
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: view.unplanned.length === 0
                        text: qsTr("Every change is planned")
                        color: view.s.text_muted
                    }
                }
            }
        }

        // The stack, newest on top
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Planned commits")
                    font.bold: true
                }
                Button {
                    styles: [Styles.ButtonStyle.small]
                    icon.source: Icons.source("plus")
                    icon.width: 16
                    icon.height: 16
                    text: qsTr("Plan a commit")
                    onClicked: view.clusters.addCluster("")
                }
                Button {
                    objectName: "commitStackButton"
                    styles: [Styles.ButtonStyle.small]
                    highlighted: true
                    icon.source: Icons.source("git-commit-vertical")
                    icon.width: 16
                    icon.height: 16
                    // The commits that have changes; empty ones are skipped
                    text: view.planned.length === 1 ? qsTr("Make 1 commit")
                                                    : qsTr("Make %1 commits").arg(view.planned.length)
                    enabled: view.planned.length > 0 && !view.composer.committing
                    onClicked: view.composer.commitAll()
                    ToolTip.visible: hovered && view.emptyPlans > 0
                    ToolTip.text: qsTr("Planned commits without changes are not made")
                }
            }
            Label {
                Layout.fillWidth: true
                visible: view.emptyPlans > 0
                horizontalAlignment: Text.AlignRight
                text: view.emptyPlans === 1 ? qsTr("1 planned commit has no changes yet and is not made")
                                            : qsTr("%1 planned commits have no changes yet and are not made").arg(view.emptyPlans)
                color: view.s.warning_default
                font.pixelSize: 11
            }

            ListView {
                id: stack

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 0
                // Newest on top: the last commit made is the first row
                model: view.clusters.clusters.slice().reverse()
                footer: Item {
                    width: stack.width
                    height: 52
                    // HEAD, what the stack is made on
                    Rectangle {
                        x: 13
                        y: 0
                        width: 2
                        height: 18
                        color: view.s.stroke_field
                    }
                    Rectangle {
                        x: 6
                        y: 18
                        width: 16
                        height: 16
                        radius: 8
                        color: view.s.background_elevated
                        border.width: 2
                        border.color: view.s.text_muted
                    }
                    Label {
                        x: 34
                        y: 16
                        text: qsTr("HEAD %1 %2").arg(view.clusters.headSha).arg(view.clusters.headSubject)
                        textFormat: Text.PlainText
                        color: view.s.text_muted
                    }
                }
                delegate: Item {
                    id: commit

                    required property var modelData
                    required property int index
                    readonly property color tint: view.colorOf(modelData.color)
                    // Its place in the order the commits are made
                    readonly property int order: view.clusters.clusters.length - 1 - index
                    readonly property bool empty: modelData.hunks.length === 0

                    width: stack.width
                    height: card.height + 12
                    opacity: view.movedCommit === modelData.id ? 0.5 : 1

                    // Where a moved commit would go
                    Rectangle {
                        visible: view.movedCommit >= 0 && view.moveTarget === commit.index
                                 && view.movedCommit !== commit.modelData.id
                        x: 32
                        y: -4
                        width: parent.width - 32
                        height: 3
                        radius: 1.5
                        color: view.s.primary_default
                    }

                    // The line of the stack and the commit's dot
                    Rectangle {
                        x: 13
                        width: 2
                        height: parent.height
                        color: view.s.stroke_field
                    }
                    Rectangle {
                        x: 6
                        y: 14
                        width: 16
                        height: 16
                        radius: 8
                        color: commit.empty ? view.s.background_elevated : commit.tint
                        border.width: 2
                        border.color: commit.tint
                    }

                    Rectangle {
                        id: card

                        x: 32
                        width: parent.width - 32
                        height: body.implicitHeight + 16
                        radius: 6
                        color: drop.containsDrag ? Qt.alpha(commit.tint, 0.12) : view.s.background_elevated
                        border.width: drop.containsDrag ? 2 : 1
                        border.color: drop.containsDrag ? commit.tint : view.s.stroke_subtle

                        DropArea {
                            id: drop
                            anchors.fill: parent
                            keys: ["hunk"]
                            onDropped: view.dropOn(commit.modelData.id)
                        }
                        ColumnLayout {
                            id: body

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 8
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                // The grip moves the commit in the stack; its
                                // handlers on an item of their own, as Icon is a
                                // disabled button
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
                                        cursorShape: Qt.SizeVerCursor
                                    }
                                    DragHandler {
                                        target: null
                                        enabled: !(parent.Window.window?.popupOpen ?? false)
                                        // The stack does not take it over to scroll
                                        grabPermissions: PointerHandler.CanTakeOverFromAnything
                                        onActiveChanged: {
                                            if (active) {
                                                view.movedCommit = commit.modelData.id
                                            } else {
                                                if (view.moveTarget >= 0)
                                                    view.clusters.moveCluster(view.movedCommit,
                                                                              view.clusters.clusters.length - 1 - view.moveTarget)
                                                view.movedCommit = -1
                                                view.moveTarget = -1
                                            }
                                        }
                                        onCentroidChanged: {
                                            if (!active)
                                                return
                                            // The row under the pointer, newest on top
                                            const p = parent.mapToItem(stack.contentItem, centroid.position)
                                            const row = stack.indexAt(stack.width / 2, p.y)
                                            if (row >= 0)
                                                view.moveTarget = row
                                        }
                                    }
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    styles: [Styles.FieldStyle.small]
                                    placeholderText: qsTr("Title")
                                    text: commit.modelData.name
                                    selectByMouse: true
                                    Component.onCompleted: cursorPosition = 0
                                    onEditingFinished: {
                                        view.clusters.renameCluster(commit.modelData.id, text)
                                        cursorPosition = 0
                                    }
                                }
                                Label {
                                    text: commit.empty ? qsTr("empty")
                                                       : `${commit.modelData.files.length} ${commit.modelData.files.length === 1 ? qsTr("file") : qsTr("files")}  +${commit.modelData.added} −${commit.modelData.removed}`
                                    color: view.s.text_muted
                                    font.pixelSize: 11
                                }
                                IconButton {
                                    iconName: "arrow-up"
                                    enabled: commit.order < view.clusters.clusters.length - 1
                                    onClicked: view.clusters.moveCluster(commit.modelData.id, commit.order + 1)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Make it later")
                                }
                                IconButton {
                                    iconName: "arrow-down"
                                    enabled: commit.order > 0
                                    onClicked: view.clusters.moveCluster(commit.modelData.id, commit.order - 1)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Make it earlier")
                                }
                                IconButton {
                                    iconName: "message-square"
                                    onClicked: view.composer.editMessage(commit.modelData.id)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Write the whole message")
                                }
                                IconButton {
                                    iconName: "trash-2"
                                    onClicked: view.clusters.removeCluster(commit.modelData.id)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Drop the plan; its hunks are unplanned again")
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: commit.modelData.hunks.map(id => view.hunkById(id)).filter(hunk => hunk)
                                    delegate: HunkCard {
                                        id: member

                                        required property var modelData

                                        width: 230
                                        hunk: modelData
                                        monoFont: view.monoFont
                                        maxLines: 0
                                        tint: commit.tint
                                        onSplitRequested: view.composer.splitHunk(modelData.hunk)
                                        opacity: view.dragged?.id === modelData.id ? 0.4 : 1
                                        HoverHandler {
                                            cursorShape: Qt.OpenHandCursor
                                        }
                                        Draggable {
                                            hunk: member.modelData
                                        }
                                    }
                                }
                                Label {
                                    visible: commit.empty
                                    text: qsTr("Drag hunks here")
                                    color: view.s.text_muted
                                }
                            }
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }

    HunkCard {
        id: ghost

        visible: view.dragged !== null
        hunk: view.dragged ?? {path: "", line: 0, added: 0, removed: 0, section: "", lines: []}
        monoFont: view.monoFont
        width: 240
        maxLines: 0
        splittable: false
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
