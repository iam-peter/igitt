pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// Concept B, a node graph: files are the sources on the left, their hunks
// the nodes in the middle, the clusters the containers on the right. A
// wire from a file to each of its hunks, and one from a hunk to its
// cluster: a file whose hunks go to several clusters shows it, as does a
// cluster that reaches into many files.
//
// Wires are drawn from a node's port, the dot on its right, onto a
// cluster; from a file's port they take all its hunks along. Pulling a
// wire off a connected hunk and letting it go over nothing takes the hunk
// out of its cluster. Pointing at a node draws the wires it touches
// stronger. The clusters stand in the order they are committed and move
// by their grip; the scissors open a hunk, so some of its lines can be
// picked and dragged onto another cluster.
Item {
    id: view

    required property ChangeClusters clusters
    // The window: commitCluster(), commitAll(), editMessage(), splitHunk(),
    // splitPiece, place()
    required property var composer
    required property font monoFont
    required property var colorOf

    readonly property Styles.Semantics s: Styles.TokenInterface.semantics

    // Columns and sizes of the nodes
    readonly property int fileX: 16
    readonly property int fileWidth: 240
    readonly property int hunkX: 330
    readonly property int hunkWidth: 300
    readonly property int hunkHeight: 80
    readonly property int clusterX: 740
    readonly property int clusterWidth: 280
    readonly property int clusterHeight: 112
    readonly property int gap: 10
    readonly property int firstY: 44

    // Where each node is: hunks in file order, a file centered on its
    // hunks, the clusters stacked
    readonly property var layout: {
        const hunkY = {}
        const fileY = {}
        const clusterY = {}
        let y = firstY
        for (const file of clusters.files) {
            const first = y
            for (const id of file.hunks) {
                hunkY[id] = y
                y += hunkHeight + gap
            }
            fileY[file.path] = (first + y - gap) / 2 - 22
            y += gap
        }
        let c = firstY
        for (const cluster of clusters.clusters) {
            clusterY[cluster.id] = c
            c += clusterHeight + gap * 2
        }
        return {hunk: hunkY, file: fileY, cluster: clusterY, newCluster: c,
                height: Math.max(y, c + clusterHeight + 20)}
    }

    // What the pointer is over, to light up what it touches
    property string hoveredFile
    property string hoveredHunk
    property int hoveredCluster: -1
    // A wire being drawn: from a port to the pointer
    property var wire: null
    // Lines carried out of a card split open, and where the pointer is
    property var carried: null
    property point carryPoint
    // A cluster moved by its grip, and where it would go in the order
    property int movedCluster: -1
    property int moveTarget: -1
    function clusterIndexAt(y: real): int {
        return Math.max(0, Math.min(clusters.clusters.length - 1,
                                    Math.floor((y - firstY + gap) / (clusterHeight + gap * 2))))
    }

    function clusterById(id: int): var {
        return clusters.clusters.find(cluster => cluster.id === id) ?? null
    }
    // The cluster node under a point of the canvas, -1 for none
    function clusterAt(point: point): int {
        if (point.x < clusterX - 12 || point.x > clusterX + clusterWidth)
            return -1
        for (const cluster of clusters.clusters) {
            const y = layout.cluster[cluster.id]
            if (point.y >= y && point.y <= y + clusterHeight)
                return cluster.id
        }
        return -1
    }
    // Where carried lines land: on a cluster, or under the clusters in a
    // new one
    function dropCarried() {
        const piece = carried
        carried = null
        const target = clusterAt(carryPoint)
        if (target >= 0)
            composer.place(piece, target)
        else if (inNewCluster(carryPoint))
            composer.place(piece, clusters.addCluster(""))
    }
    function inNewCluster(point: point): bool {
        return point.x >= clusterX && point.x <= clusterX + clusterWidth
               && point.y >= layout.newCluster && point.y <= layout.newCluster + clusterHeight
    }
    // Lit: what the pointer is over, and what is wired to it
    function hunkLit(hunk: var): bool {
        return hunk.id === hoveredHunk || hunk.path === hoveredFile
               || (hoveredCluster >= 0 && hunk.cluster === hoveredCluster)
    }
    // The clusters a file's hunks go to
    function clustersOfFile(path: string): var {
        const ids = new Set(clusters.hunks.filter(h => h.path === path && h.cluster >= 0).map(h => h.cluster))
        return Array.from(ids)
    }

    // The dot a wire starts from
    component Port: Rectangle {
        id: port

        // What the wire carries: a hunk's id or a file's path
        required property string hunkId
        required property string filePath
        property color tint: view.s.text_muted

        width: 12
        height: 12
        radius: 6
        color: view.s.background_elevated
        border.width: 2
        border.color: tint

        HoverHandler {
            cursorShape: Qt.CrossCursor
        }
        DragHandler {
            target: null
            // Not under a dialog or menu (ApplicationWindow.popupOpen)
            enabled: !(parent.Window.window?.popupOpen ?? false)
            onActiveChanged: {
                if (active)
                    return
                const end = view.wire ? view.wire.to : null
                view.wire = null
                if (!end)
                    return
                const target = view.clusterAt(end)
                if (port.filePath !== "")
                    view.clusters.assignFile(port.filePath, target)
                else
                    view.clusters.assign(port.hunkId, target)
            }
            onCentroidChanged: {
                if (!active)
                    return
                const from = port.mapToItem(canvas, port.width / 2, port.height / 2)
                const to = port.mapToItem(canvas, centroid.position.x, centroid.position.y)
                view.wire = {from: from, to: to, color: port.tint}
            }
        }
    }

    Flickable {
        id: flick

        anchors.fill: parent
        clip: true
        contentWidth: canvas.width
        contentHeight: canvas.height
        boundsBehavior: Flickable.StopAtBounds
        // Dragging draws wires; the wheel and the bars scroll
        interactive: false
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        WheelHandler {
            target: null
            onWheel: event => {
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                                      flick.contentY - event.angleDelta.y))
            }
        }

        Item {
            id: canvas

            width: Math.max(view.clusterX + view.clusterWidth + 24, flick.width)
            height: Math.max(view.layout.height, flick.height)

            // Column titles
            Repeater {
                model: [{x: view.fileX, text: qsTr("Files")}, {x: view.hunkX, text: qsTr("Hunks")},
                        {x: view.clusterX, text: qsTr("Clusters")}]
                delegate: Label {
                    required property var modelData
                    x: modelData.x
                    y: 12
                    text: modelData.text
                    font.bold: true
                    color: view.s.text_muted
                }
            }
            Button {
                x: view.clusterX + view.clusterWidth - width
                y: 6
                styles: [Styles.ButtonStyle.small]
                icon.source: Icons.source("plus")
                icon.width: 16
                icon.height: 16
                text: qsTr("New cluster")
                onClicked: view.clusters.addCluster("")
            }

            // The wires, under the nodes
            Canvas {
                id: wires

                anchors.fill: parent
                // Everything that moves a wire
                readonly property var inputs: [view.layout, view.clusters.hunks, view.clusters.clusters, view.wire,
                                               view.hoveredFile, view.hoveredHunk, view.hoveredCluster]
                onInputsChanged: requestPaint()

                function curve(ctx, x1, y1, x2, y2) {
                    const bend = Math.max(40, (x2 - x1) / 2)
                    ctx.beginPath()
                    ctx.moveTo(x1, y1)
                    ctx.bezierCurveTo(x1 + bend, y1, x2 - bend, y2, x2, y2)
                    ctx.stroke()
                }

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.lineCap = "round"
                    for (const hunk of view.clusters.hunks) {
                        const lit = view.hunkLit(hunk)
                        const hy = view.layout.hunk[hunk.id] + view.hunkHeight / 2
                        const fy = view.layout.file[hunk.path] + 22
                        // File to hunk
                        ctx.lineWidth = lit ? 2 : 1.25
                        ctx.strokeStyle = Qt.alpha(view.s.text_muted, lit ? 0.9 : 0.45)
                        curve(ctx, view.fileX + view.fileWidth + 6, fy, view.hunkX, hy)
                        // Hunk to its cluster
                        const cluster = view.clusterById(hunk.cluster)
                        if (!cluster)
                            continue
                        const cy = view.layout.cluster[cluster.id] + view.clusterHeight / 2
                        ctx.lineWidth = lit ? 3 : 2
                        ctx.strokeStyle = Qt.alpha(view.colorOf(cluster.color), lit ? 1 : 0.75)
                        curve(ctx, view.hunkX + view.hunkWidth + 6, hy, view.clusterX - 6, cy)
                    }
                    if (view.wire) {
                        ctx.lineWidth = 2
                        ctx.setLineDash([6, 4])
                        ctx.strokeStyle = view.wire.color
                        curve(ctx, view.wire.from.x, view.wire.from.y, view.wire.to.x, view.wire.to.y)
                    }
                }
            }

            // Files
            Repeater {
                model: view.clusters.files
                delegate: Rectangle {
                    id: fileNode

                    required property var modelData
                    readonly property var owners: view.clustersOfFile(modelData.path)
                    readonly property bool lit: view.hoveredFile === modelData.path
                                                || view.clusters.hunks.some(h => h.path === modelData.path
                                                                                 && view.hunkLit(h))

                    x: view.fileX
                    y: view.layout.file[modelData.path] ?? 0
                    width: view.fileWidth
                    height: 44
                    radius: 6
                    color: view.s.background_elevated
                    border.width: lit ? 2 : 1
                    border.color: lit ? view.s.stroke_focus : view.s.stroke_subtle
                    HoverHandler {
                        onHoveredChanged: view.hoveredFile = hovered ? fileNode.modelData.path : ""
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 14
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: fileNode.modelData.path.split("/").pop()
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            font.bold: true
                        }
                        // A file split between clusters may tie them together
                        Label {
                            Layout.fillWidth: true
                            text: fileNode.owners.length > 1
                                  ? qsTr("split across %1 clusters").arg(fileNode.owners.length)
                                  : fileNode.modelData.path
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            color: fileNode.owners.length > 1 ? view.s.warning_default : view.s.text_muted
                            font.pixelSize: 11
                        }
                    }
                    Port {
                        x: parent.width - 6
                        anchors.verticalCenter: parent.verticalCenter
                        hunkId: ""
                        filePath: fileNode.modelData.path
                    }
                }
            }

            // Hunks
            Repeater {
                model: view.clusters.hunks
                delegate: HunkCard {
                    id: hunkNode

                    required property var modelData
                    readonly property var cluster: view.clusterById(modelData.cluster)

                    x: view.hunkX
                    y: view.layout.hunk[modelData.id] ?? 0
                    // Split open, it grows over the hunks below it
                    z: splitting ? 3 : 0
                    width: view.hunkWidth
                    height: splitting ? Math.max(view.hunkHeight, implicitHeight) : view.hunkHeight
                    clip: true
                    hunk: modelData
                    splitting: view.composer.splitPiece === modelData.id
                    onCarryStarted: piece => view.carried = piece
                    onCarryMoved: position => view.carryPoint = hunkNode.mapToItem(canvas, position)
                    onCarryEnded: view.dropCarried()
                    monoFont: view.monoFont
                    showPath: false
                    maxLines: 1
                    tint: cluster ? view.colorOf(cluster.color) : Styles.TokenInterface.transparent
                    border.width: view.hunkLit(modelData) ? 2 : 1
                    border.color: view.hunkLit(modelData) ? view.s.stroke_focus : view.s.stroke_subtle
                    onSplitRequested: view.composer.splitHunk(modelData.id)

                    HoverHandler {
                        onHoveredChanged: view.hoveredHunk = hovered ? hunkNode.modelData.id : ""
                    }
                    Port {
                        visible: !hunkNode.splitting
                        x: parent.width - 6
                        anchors.verticalCenter: parent.verticalCenter
                        hunkId: hunkNode.modelData.id
                        filePath: ""
                        tint: hunkNode.cluster ? hunkNode.tint : view.s.text_muted
                    }
                }
            }

            // Clusters
            Repeater {
                model: view.clusters.clusters
                delegate: Rectangle {
                    id: clusterNode

                    required property var modelData
                    required property int index
                    readonly property color tint: view.colorOf(modelData.color)
                    readonly property bool target: (view.wire !== null && view.clusterAt(view.wire.to) === modelData.id)
                                                   || (view.carried !== null
                                                       && view.clusterAt(view.carryPoint) === modelData.id)
                    readonly property bool lit: view.hoveredCluster === modelData.id
                                                || view.clusters.hunks.some(h => h.cluster === modelData.id
                                                                                 && view.hunkLit(h))

                    x: view.clusterX
                    y: view.layout.cluster[modelData.id] ?? 0
                    width: view.clusterWidth
                    height: view.clusterHeight
                    radius: 8
                    color: target ? Qt.alpha(tint, 0.15) : view.s.background_elevated
                    border.width: target || lit ? 2 : 1
                    border.color: target || lit ? tint : view.s.stroke_subtle
                    opacity: view.movedCluster === modelData.id ? 0.6 : 1

                    HoverHandler {
                        onHoveredChanged: view.hoveredCluster = hovered ? clusterNode.modelData.id : -1
                    }
                    // Where a moved cluster would go
                    Rectangle {
                        visible: view.movedCluster >= 0 && view.moveTarget === clusterNode.index
                                 && view.movedCluster !== clusterNode.modelData.id
                        y: -view.gap + 1
                        width: parent.width
                        height: 3
                        radius: 1.5
                        color: view.s.primary_default
                    }
                    // The input every wire ends at
                    Rectangle {
                        x: -6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        radius: 6
                        color: clusterNode.tint
                    }
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.leftMargin: 12
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            // The grip moves the cluster in the order; its
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
                                    onActiveChanged: {
                                        if (active) {
                                            view.movedCluster = clusterNode.modelData.id
                                        } else {
                                            if (view.moveTarget >= 0)
                                                view.clusters.moveCluster(view.movedCluster, view.moveTarget)
                                            view.movedCluster = -1
                                            view.moveTarget = -1
                                        }
                                    }
                                    onCentroidChanged: {
                                        if (active)
                                            view.moveTarget = view.clusterIndexAt(
                                                parent.mapToItem(canvas, centroid.position).y)
                                    }
                                }
                            }
                            Rectangle {
                                implicitWidth: 20
                                implicitHeight: 20
                                radius: 10
                                color: clusterNode.tint
                                Label {
                                    anchors.centerIn: parent
                                    text: clusterNode.index + 1
                                    color: Styles.TokenInterface.textOn(clusterNode.tint)
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }
                            TextField {
                                Layout.fillWidth: true
                                styles: [Styles.FieldStyle.small]
                                text: clusterNode.modelData.name
                                selectByMouse: true
                                Component.onCompleted: cursorPosition = 0
                                onEditingFinished: {
                                    view.clusters.renameCluster(clusterNode.modelData.id, text)
                                    cursorPosition = 0
                                }
                            }
                            IconButton {
                                iconName: "message-square"
                                onClicked: view.composer.editMessage(clusterNode.modelData.id)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Write the whole message")
                            }
                            IconButton {
                                iconName: "trash-2"
                                onClicked: view.clusters.removeCluster(clusterNode.modelData.id)
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: clusterNode.modelData.hunks.length === 0
                                  ? qsTr("Draw a wire from a hunk here")
                                  : qsTr("%n hunk(s) in %1 file(s)", "", clusterNode.modelData.hunks.length)
                                        .arg(clusterNode.modelData.files.length)
                            elide: Text.ElideRight
                            color: view.s.text_muted
                            font.pixelSize: 11
                        }
                        Item {
                            Layout.fillHeight: true
                        }
                        Button {
                            Layout.fillWidth: true
                            styles: [Styles.ButtonStyle.small]
                            highlighted: true
                            icon.source: Icons.source("git-commit-horizontal")
                            icon.width: 16
                            icon.height: 16
                            text: qsTr("Commit")
                            enabled: clusterNode.modelData.hunks.length > 0 && !view.composer.committing
                            onClicked: view.composer.commitCluster(clusterNode.modelData.id)
                        }
                    }
                }
            }

            // Where carried lines make a cluster of their own
            Rectangle {
                objectName: "newClusterDrop"
                readonly property bool target: view.carried !== null && view.inNewCluster(view.carryPoint)

                visible: view.carried !== null
                x: view.clusterX
                y: view.layout.newCluster
                width: view.clusterWidth
                height: view.clusterHeight
                radius: 8
                color: target ? Qt.alpha(view.s.primary_default, 0.08) : Styles.TokenInterface.transparent
                border.width: 1
                border.color: target ? view.s.primary_default : view.s.stroke_field
                Label {
                    anchors.centerIn: parent
                    text: qsTr("Drop here for a new cluster")
                    color: view.s.text_muted
                }
            }

            // What follows the pointer while lines are carried
            HunkCard {
                visible: view.carried !== null
                hunk: view.carried ?? {path: "", line: 0, added: 0, removed: 0, section: "", lines: []}
                monoFont: view.monoFont
                splittable: false
                showPath: false
                width: 240
                maxLines: 1
                x: view.carryPoint.x - 20
                y: view.carryPoint.y - 12
                z: 10
                opacity: 0.9
            }
        }
    }
}
