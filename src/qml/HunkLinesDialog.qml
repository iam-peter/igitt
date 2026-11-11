pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// The lines of one hunk, each with the cluster it is in: lines are picked
// (a click, Shift+click for a range) and moved to a cluster of their own,
// so one hunk can go into several commits.
ModalDialog {
    id: dialog

    required property ChangeClusters clusters
    required property font monoFont
    required property var colorOf

    property string hunkId
    property var hunk: ({lines: []})
    // The chosen lines, by their index in the hunk
    property var chosen: []
    property int anchorRow: -1

    readonly property Styles.Semantics s: Styles.TokenInterface.semantics
    readonly property var changedRows: hunk.lines.filter(line => line.changed)

    function edit(id: string) {
        hunkId = id
        chosen = []
        anchorRow = -1
        refresh()
        open()
    }
    function refresh() {
        hunk = clusters.hunkLines(hunkId)
        if (!hunk.lines)
            hunk = {lines: []}
    }
    function clusterById(id: int): var {
        return clusters.clusters.find(cluster => cluster.id === id) ?? null
    }
    function toggle(row: int, range: bool) {
        const line = hunk.lines[row]
        if (!line.changed)
            return
        let picked = Array.from(chosen)
        if (range && anchorRow >= 0) {
            const [from, to] = [Math.min(anchorRow, row), Math.max(anchorRow, row)]
            for (let r = from; r <= to; ++r) {
                if (hunk.lines[r].changed && !picked.includes(hunk.lines[r].index))
                    picked.push(hunk.lines[r].index)
            }
        } else if (picked.includes(line.index)) {
            picked = picked.filter(index => index !== line.index)
        } else {
            picked.push(line.index)
        }
        anchorRow = row
        chosen = picked
    }
    function moveTo(clusterId: int) {
        if (chosen.length === 0)
            return
        clusters.assignLines(hunkId, chosen, clusterId)
        chosen = []
        refresh()
    }

    width: Math.min(760, parent ? parent.width - 40 : 760)
    height: Math.min(560, parent ? parent.height - 40 : 560)
    title: qsTr("Split %1:%2").arg(hunk.path ?? "").arg(hunk.line ?? "")
    closeButton: true

    Connections {
        target: dialog.clusters
        function onChanged() {
            if (dialog.visible)
                dialog.refresh()
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Pick the lines that belong elsewhere, Shift+click for a range, then move them.")
            color: dialog.s.text_muted
        }
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0

            ListView {
                id: lines

                anchors.fill: parent
                anchors.margins: 1
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: dialog.hunk.lines
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row

                    required property var modelData
                    required property int index
                    readonly property bool picked: dialog.chosen.includes(modelData.index)
                    readonly property var cluster: dialog.clusterById(modelData.cluster)

                    width: ListView.view.width
                    height: text.implicitHeight + 2
                    color: picked ? dialog.s.selection_default
                         : modelData.kind === "+" ? dialog.s.diff_added
                         : modelData.kind === "-" ? dialog.s.diff_removed
                         : Styles.TokenInterface.transparent

                    // The cluster the line is in
                    Rectangle {
                        width: 4
                        height: parent.height
                        visible: row.modelData.changed
                        color: row.cluster ? dialog.colorOf(row.cluster.color) : dialog.s.stroke_field
                    }
                    Text {
                        id: text
                        x: 10
                        width: parent.width - x - clusterName.width - 12
                        text: row.modelData.kind + row.modelData.text
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: row.modelData.changed ? dialog.s.text_default : dialog.s.text_muted
                        font.family: dialog.monoFont.family
                        font.pointSize: dialog.monoFont.pointSize
                    }
                    Label {
                        id: clusterName
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.modelData.changed
                        text: row.cluster ? row.cluster.name : qsTr("not in a cluster")
                        color: dialog.s.text_muted
                        font.pixelSize: 11
                    }
                    TapHandler {
                        onTapped: (point, button) => dialog.toggle(row.index, point.modifiers & Qt.ShiftModifier)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: qsTr("Move %n line(s) to", "", dialog.chosen.length)
            }
            ComboBox {
                id: target
                Layout.fillWidth: true
                styles: [Styles.FieldStyle.medium]
                textRole: "name"
                valueRole: "id"
                model: dialog.clusters.clusters.concat([{id: -1, name: qsTr("No cluster")},
                                                        {id: -2, name: qsTr("A new cluster")}])
            }
            Button {
                objectName: "moveLinesButton"
                styles: [Styles.ButtonStyle.medium]
                highlighted: true
                text: qsTr("Move")
                enabled: dialog.chosen.length > 0
                onClicked: dialog.moveTo(target.currentValue === -2 ? dialog.clusters.addCluster("")
                                                                     : target.currentValue)
            }
        }
    }
}
