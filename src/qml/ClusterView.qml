pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// The plainest view of the clusters, the base the concepts start from:
// every hunk with a choice of its cluster, and the clusters with their
// commit buttons.
Item {
    id: view

    required property ChangeClusters clusters
    required property font monoFont
    required property var colorOf

    function clusterById(id: int): var {
        return clusters.clusters.find(cluster => cluster.id === id) ?? null
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: view.clusters.hunks
            delegate: RowLayout {
                id: row

                required property var modelData

                width: ListView.view.width
                spacing: 8

                HunkCard {
                    Layout.fillWidth: true
                    hunk: row.modelData
                    monoFont: view.monoFont
                    tint: row.modelData.cluster >= 0
                          ? view.colorOf(view.clusterById(row.modelData.cluster)?.color ?? 0)
                          : Styles.TokenInterface.transparent
                }
                ComboBox {
                    Layout.preferredWidth: 200
                    styles: [Styles.FieldStyle.small]
                    textRole: "name"
                    valueRole: "id"
                    model: [{id: -1, name: qsTr("No cluster")}].concat(view.clusters.clusters)
                    currentIndex: count > 0 ? Math.max(0, indexOfValue(row.modelData.cluster)) : -1
                    onActivated: view.clusters.assign(row.modelData.id, currentValue)
                }
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 320
            Layout.maximumWidth: 320
            Layout.fillHeight: true
            spacing: 8

            Button {
                text: qsTr("New cluster")
                styles: [Styles.ButtonStyle.small]
                onClicked: view.clusters.addCluster("")
            }
            Repeater {
                model: view.clusters.clusters
                delegate: RowLayout {
                    id: clusterRow

                    required property var modelData

                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        styles: [Styles.FieldStyle.small]
                        text: clusterRow.modelData.name
                        onEditingFinished: view.clusters.renameCluster(clusterRow.modelData.id, text)
                    }
                    Button {
                        styles: [Styles.ButtonStyle.small]
                        text: qsTr("Commit %n hunk(s)", "", clusterRow.modelData.hunks.length)
                        enabled: clusterRow.modelData.hunks.length > 0
                        onClicked: view.clusters.commitCluster(clusterRow.modelData.id, clusterRow.modelData.name)
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }
    }
}
