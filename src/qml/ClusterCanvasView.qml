pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// Concept C, an infinite canvas: every hunk is a card that can go
// anywhere, every cluster a frame. A card belongs to the frame it is
// dropped into, so work is grouped by putting it together; moving a frame
// takes its cards along at once, and a frame moved or made larger over a
// card does not take it in. The canvas pans by dragging its empty space
// and zooms with the wheel, so a large change set can be laid out and
// found again where it was put. The chips at the top are the clusters in
// the order they are committed, dragged to change it; the scissors open a
// card, so some of its lines can be picked and dragged into another frame
// or out of every frame.
Item {
    id: view

    required property ChangeClusters clusters
    // The window: commitCluster(), commitAll(), editMessage(), splitHunk(),
    // splitPiece, place()
    required property var composer
    required property font monoFont
    required property var colorOf

    readonly property Styles.Semantics s: Styles.TokenInterface.semantics
    readonly property int cardWidth: 220
    readonly property int cardHeight: 96
    // A frame being dragged, and how far: its cards follow at once
    property int movingFrame: -1
    property point moveOffset
    // Lines carried out of a card split open, and where the pointer is in
    // the world
    property var carried: null
    property point carryPoint
    // A chip of the order being dragged, and where it would go
    property int movedChip: -1
    property int chipTarget: -1
    // Nothing on the canvas moves under a dialog or menu
    readonly property bool blocked: Window.window?.popupOpen ?? false

    // Cards not placed yet go in columns by file at the left, the inbox
    readonly property var placement: {
        const at = {}
        let column = 0
        let row = 0
        const loose = new Set(clusters.hunks.filter(hunk => hunk.cluster < 0).map(hunk => hunk.id))
        for (const file of clusters.files) {
            for (const id of file.hunks.filter(id => loose.has(id))) {
                at[id] = Qt.point(40 + column * (cardWidth + 16), 104 + row * (cardHeight + 12))
                if (++row >= 6) {
                    row = 0
                    ++column
                }
            }
        }
        return at
    }
    // Where a card is: where it was put, else in its frame's grid, else in
    // the inbox
    function positionOf(hunk: var): point {
        if (hunk.x >= 0)
            return Qt.point(hunk.x, hunk.y)
        const index = clusters.clusters.findIndex(c => c.id === hunk.cluster)
        if (index >= 0) {
            const cluster = clusters.clusters[index]
            const r = frameRect(cluster, index)
            const slot = cluster.hunks.indexOf(hunk.id)
            const columns = Math.max(1, Math.floor((r.width - 16) / (cardWidth + 12)))
            return Qt.point(r.x + 12 + (slot % columns) * (cardWidth + 12),
                            r.y + 52 + Math.floor(slot / columns) * (cardHeight + 12))
        }
        return placement[hunk.id] ?? Qt.point(40, 104)
    }
    // Frames that have no place yet: two side by side right of the inbox,
    // each as high as its cards need
    readonly property var defaultFrames: {
        const rects = {}
        let y = 96
        const list = clusters.clusters
        for (let i = 0; i < list.length; i += 2) {
            let rowHeight = 0
            for (let j = i; j < Math.min(i + 2, list.length); ++j) {
                const rows = Math.max(1, Math.ceil(list[j].hunks.length / 2))
                const height = 64 + rows * (cardHeight + 12)
                rects[list[j].id] = Qt.rect(320 + (j - i) * 500, y, 480, height)
                rowHeight = Math.max(rowHeight, height)
            }
            y += rowHeight + 24
        }
        return rects
    }
    function frameRect(cluster: var, index: int): rect {
        if (cluster.width > 0)
            return Qt.rect(cluster.x, cluster.y, cluster.width, cluster.height)
        return defaultFrames[cluster.id] ?? Qt.rect(320, 60, 480, 200)
    }
    // The frame a point of the world lies in, -1 for none; the last drawn
    // one wins, as it is on top
    function frameAt(point: point): int {
        const list = clusters.clusters
        for (let i = list.length - 1; i >= 0; --i) {
            const r = frameRect(list[i], i)
            if (point.x >= r.x && point.x <= r.x + r.width && point.y >= r.y && point.y <= r.y + r.height)
                return list[i].id
        }
        return -1
    }
    // A card was put somewhere: it is there now, and in the frame there
    function placeCard(hunk: var, x: real, y: real) {
        clusters.setHunkPosition(hunk.id, x, y)
        const cluster = frameAt(Qt.point(x + cardWidth / 2, y + cardHeight / 2))
        if (cluster !== hunk.cluster)
            clusters.assign(hunk.id, cluster)
    }
    // Carried lines land in the frame under the pointer, or out of every
    // frame; a card of their own shows where they were let go
    function dropCarried() {
        const piece = carried
        carried = null
        const cluster = frameAt(carryPoint)
        const id = piece.hunk + "/" + (cluster < 0 ? "none" : cluster)
        if (!clusters.hunks.some(hunk => hunk.id === id))
            clusters.setHunkPosition(id, carryPoint.x - 20, carryPoint.y - 12)
        composer.place(piece, cluster)
    }
    // A frame moved or changed its size: its cards move along
    function placeFrame(cluster: var, index: int, x: real, y: real, width: real, height: real) {
        const old = frameRect(cluster, index)
        const dx = x - old.x
        const dy = y - old.y
        if (dx !== 0 || dy !== 0) {
            for (const hunk of clusters.hunks) {
                if (hunk.cluster === cluster.id) {
                    const p = positionOf(hunk)
                    clusters.setHunkPosition(hunk.id, p.x + dx, p.y + dy)
                }
            }
        }
        // Only a card that is dropped changes its frame: a frame moved or
        // made larger over another card leaves it where it was
        clusters.setClusterGeometry(cluster.id, x, y, width, height)
    }
    // A new frame, in the middle of what is shown
    function addFrame() {
        const id = clusters.addCluster("")
        const middle = world.mapFromItem(view, view.width / 2, view.height / 2)
        clusters.setClusterGeometry(id, middle.x - 280, middle.y - 160, 560, 320)
    }
    function zoomTo(factor: real, at: point) {
        const before = world.mapFromItem(view, at.x, at.y)
        world.scale = Math.max(0.25, Math.min(2, factor))
        const after = world.mapToItem(view, before.x, before.y)
        world.x += at.x - after.x
        world.y += at.y - after.y
    }

    clip: true

    // The paper: dots that move with the world
    Rectangle {
        anchors.fill: parent
        color: view.s.background_default
    }
    Canvas {
        anchors.fill: parent
        readonly property var inputs: [world.x, world.y, world.scale, view.s.stroke_field]
        onInputsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = view.s.stroke_field
            const step = 24 * world.scale
            if (step < 6)
                return
            const ox = ((world.x % step) + step) % step
            const oy = ((world.y % step) + step) % step
            for (let x = ox; x < width; x += step)
                for (let y = oy; y < height; y += step)
                    ctx.fillRect(x, y, 1.5, 1.5)
        }
    }

    // Panning: the empty space moves the world. On an item of its own
    // under the world, so a card or frame under the pointer moves instead.
    Item {
        anchors.fill: parent

        DragHandler {
            id: pan
            target: null
            enabled: !view.blocked
            grabPermissions: PointerHandler.ApprovesTakeOverByAnything
            property point start
            onActiveChanged: if (active) start = Qt.point(world.x, world.y)
            onTranslationChanged: {
                if (active) {
                    world.x = start.x + translation.x
                    world.y = start.y + translation.y
                }
            }
        }
    }
    WheelHandler {
        target: null
        onWheel: event => view.zoomTo(world.scale * Math.pow(1.0015, event.angleDelta.y),
                                      Qt.point(event.x, event.y))
    }

    Item {
        id: world
        objectName: "world"

        transformOrigin: Item.TopLeft

        // Frames under the cards
        Repeater {
            model: view.clusters.clusters
            delegate: Rectangle {
                id: frame

                required property var modelData
                required property int index
                readonly property rect area: view.frameRect(modelData, index)
                readonly property color tint: view.colorOf(modelData.color)

                // Bound to the model again after a drag moved it by hand
                function follow() {
                    x = Qt.binding(() => frame.area.x)
                    y = Qt.binding(() => frame.area.y)
                    width = Qt.binding(() => frame.area.width)
                    height = Qt.binding(() => frame.area.height)
                }

                x: area.x
                y: area.y
                width: area.width
                height: area.height
                // The place it first gets stays its own: the order changes,
                // and the frames laid out by it would move
                Component.onCompleted: {
                    if (modelData.width > 0)
                        return
                    // After the frames are made: the model changes with it
                    const id = modelData.id
                    const r = area
                    Qt.callLater(() => view.clusters.setClusterGeometry(id, r.x, r.y, r.width, r.height))
                }
                // Its cards follow while it is dragged
                onXChanged: if (frameDrag.active) view.moveOffset = Qt.point(x - area.x, y - area.y)
                onYChanged: if (frameDrag.active) view.moveOffset = Qt.point(x - area.x, y - area.y)
                // Where carried lines would go
                readonly property bool target: view.carried !== null && view.frameAt(view.carryPoint) === modelData.id
                radius: 10
                color: Qt.alpha(tint, target ? 0.18 : 0.08)
                border.width: target ? 3 : 2
                border.color: Qt.alpha(tint, target ? 1 : 0.7)

                // The title bar moves the frame
                Rectangle {
                    id: titleBar

                    width: parent.width
                    height: 40
                    radius: 10
                    color: Qt.alpha(frame.tint, 0.18)

                    DragHandler {
                        id: frameDrag
                        target: frame
                        enabled: !view.blocked
                        grabPermissions: PointerHandler.CanTakeOverFromAnything
                        cursorShape: Qt.SizeAllCursor
                        onActiveChanged: {
                            if (active) {
                                view.movingFrame = frame.modelData.id
                            } else {
                                view.placeFrame(frame.modelData, frame.index, frame.x, frame.y, frame.width, frame.height)
                                view.movingFrame = -1
                                view.moveOffset = Qt.point(0, 0)
                                frame.follow()
                            }
                        }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 6

                        Rectangle {
                            implicitWidth: 20
                            implicitHeight: 20
                            radius: 10
                            color: frame.tint
                            Label {
                                anchors.centerIn: parent
                                text: frame.index + 1
                                color: Styles.TokenInterface.textOn(frame.tint)
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                        TextField {
                            Layout.preferredWidth: Math.min(implicitWidth, frame.width - 270)
                            Layout.minimumWidth: 120
                            styles: [Styles.FieldStyle.small]
                            text: frame.modelData.name
                            selectByMouse: true
                            Component.onCompleted: cursorPosition = 0
                            onEditingFinished: {
                                view.clusters.renameCluster(frame.modelData.id, text)
                                cursorPosition = 0
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: frame.modelData.hunks.length === 0 ? qsTr("Put cards in here")
                                                                     : qsTr("%n hunk(s)", "", frame.modelData.hunks.length)
                            elide: Text.ElideRight
                            color: view.s.text_muted
                            font.pixelSize: 11
                        }
                        Button {
                            styles: [Styles.ButtonStyle.small]
                            highlighted: true
                            icon.source: Icons.source("git-commit-horizontal")
                            icon.width: 16
                            icon.height: 16
                            text: qsTr("Commit")
                            enabled: frame.modelData.hunks.length > 0 && !view.composer.committing
                            onClicked: view.composer.commitCluster(frame.modelData.id)
                        }
                        IconButton {
                            iconName: "message-square"
                            onClicked: view.composer.editMessage(frame.modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Write the whole message")
                        }
                        IconButton {
                            iconName: "trash-2"
                            onClicked: view.clusters.removeCluster(frame.modelData.id)
                        }
                    }
                }

                // The corner changes its size
                Rectangle {
                    id: corner

                    x: parent.width - width - 2
                    y: parent.height - height - 2
                    width: 16
                    height: 16
                    radius: 3
                    color: Qt.alpha(frame.tint, 0.5)

                    DragHandler {
                        target: null
                        enabled: !view.blocked
                        cursorShape: Qt.SizeFDiagCursor
                        property size start
                        onActiveChanged: {
                            if (active) {
                                start = Qt.size(frame.width, frame.height)
                            } else {
                                view.placeFrame(frame.modelData, frame.index, frame.x, frame.y, frame.width, frame.height)
                                frame.follow()
                            }
                        }
                        onTranslationChanged: {
                            if (active) {
                                frame.width = Math.max(280, start.width + translation.x / world.scale)
                                frame.height = Math.max(160, start.height + translation.y / world.scale)
                            }
                        }
                    }
                }
            }
        }

        // Cards
        Repeater {
            model: view.clusters.hunks
            delegate: HunkCard {
                id: card

                required property var modelData
                readonly property point at: view.positionOf(modelData)
                readonly property var cluster: view.clusters.clusters.find(c => c.id === modelData.cluster) ?? null
                // Where it shows: with its frame while that is dragged
                readonly property point shown: view.movingFrame >= 0 && view.movingFrame === modelData.cluster
                                               ? Qt.point(at.x + view.moveOffset.x, at.y + view.moveOffset.y) : at

                x: shown.x
                y: shown.y
                // Split open, it grows over the cards below it
                z: splitting ? 3 : cardDrag.active ? 2 : 1
                width: view.cardWidth
                height: splitting ? Math.max(view.cardHeight, implicitHeight) : view.cardHeight
                clip: true
                hunk: modelData
                monoFont: view.monoFont
                maxLines: 2
                tint: cluster ? view.colorOf(cluster.color) : Styles.TokenInterface.transparent
                splitting: view.composer.splitPiece === modelData.id
                onSplitRequested: view.composer.splitHunk(modelData.id)
                onCarryStarted: piece => view.carried = piece
                onCarryMoved: position => view.carryPoint = card.mapToItem(world, position)
                onCarryEnded: view.dropCarried()

                // A shadow while it is lifted
                border.width: cardDrag.active || splitting ? 2 : 1
                border.color: cardDrag.active || splitting ? view.s.stroke_focus : view.s.stroke_subtle

                HoverHandler {
                    enabled: !card.splitting
                    cursorShape: Qt.OpenHandCursor
                }
                DragHandler {
                    id: cardDrag
                    // A card split open picks and carries its lines itself
                    enabled: !view.blocked && !card.splitting
                    cursorShape: Qt.ClosedHandCursor
                    grabPermissions: PointerHandler.CanTakeOverFromAnything
                    onActiveChanged: {
                        if (active)
                            return
                        view.placeCard(card.modelData, card.x, card.y)
                        // Bound to the model again, so a frame takes it along
                        card.x = Qt.binding(() => card.shown.x)
                        card.y = Qt.binding(() => card.shown.y)
                    }
                }
            }
        }

        Label {
            x: 40
            y: 76
            text: qsTr("Inbox: put each card into a frame, anywhere on the canvas")
            color: view.s.text_muted
        }

        // What follows the pointer while lines are carried
        HunkCard {
            visible: view.carried !== null
            hunk: view.carried ?? {path: "", line: 0, added: 0, removed: 0, section: "", lines: []}
            monoFont: view.monoFont
            splittable: false
            width: view.cardWidth
            maxLines: 2
            x: view.carryPoint.x - 20
            y: view.carryPoint.y - 12
            z: 10
            opacity: 0.9
        }
    }

    // The order the clusters are committed in: a chip each, dragged to
    // another place
    Row {
        id: order

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 6
        visible: view.clusters.clusters.length > 0

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Commit order")
            color: view.s.text_muted
        }
        Repeater {
            model: view.clusters.clusters
            delegate: Rectangle {
                id: chip

                required property var modelData
                required property int index
                readonly property color tint: view.colorOf(modelData.color)

                width: chipLabel.implicitWidth + 34
                height: 26
                radius: 13
                color: view.s.background_elevated
                border.width: 1
                border.color: view.movedChip >= 0 && view.chipTarget === index && view.movedChip !== modelData.id
                              ? view.s.primary_default : tint
                opacity: view.movedChip === modelData.id ? 0.5 : 1

                Rectangle {
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    radius: 10
                    color: chip.tint
                    Label {
                        anchors.centerIn: parent
                        text: chip.index + 1
                        color: Styles.TokenInterface.textOn(chip.tint)
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
                Label {
                    id: chipLabel
                    x: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: chip.modelData.name.length > 28 ? chip.modelData.name.slice(0, 27) + "…" : chip.modelData.name
                    font.pixelSize: 12
                }
                HoverHandler {
                    cursorShape: Qt.SizeHorCursor
                }
                DragHandler {
                    target: null
                    enabled: !view.blocked
                    onActiveChanged: {
                        if (active) {
                            view.movedChip = chip.modelData.id
                        } else {
                            if (view.chipTarget >= 0)
                                view.clusters.moveCluster(view.movedChip, view.chipTarget)
                            view.movedChip = -1
                            view.chipTarget = -1
                        }
                    }
                    onCentroidChanged: {
                        if (!active)
                            return
                        // The chip under the pointer is where it goes
                        const x = chip.mapToItem(order, centroid.position).x
                        let target = 0
                        for (let i = 1; i < order.children.length; ++i) {
                            const other = order.children[i]
                            if (other.index !== undefined && x >= other.x)
                                target = other.index
                        }
                        view.chipTarget = target
                    }
                }
            }
        }
    }

    // What stays in place over the canvas
    RowLayout {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        spacing: 6

        Button {
            styles: [Styles.ButtonStyle.small]
            icon.source: Icons.source("square-dashed")
            icon.width: 16
            icon.height: 16
            text: qsTr("New frame")
            onClicked: view.addFrame()
        }
        Button {
            styles: [Styles.ButtonStyle.small]
            text: qsTr("%1 %").arg(Math.round(world.scale * 100))
            onClicked: {
                world.scale = 1
                world.x = 0
                world.y = 0
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Back to 100 % at the start; the wheel zooms")
        }
    }
}
