pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

// The pending actions of a plan: what each one runs, and what executing
// the plan will do.
Pane {
    id: pane

    required property Repository repo
    required property font monoFont

    signal discardRequested()

    padding: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            Label {
                anchors.fill: parent
                anchors.leftMargin: 6
                verticalAlignment: Text.AlignVCenter
                font.bold: true
                text: qsTr("Plan (%1)").arg(pane.repo.plan.count)
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            visible: pane.repo.plan.count === 0
            wrapMode: Text.Wrap
            opacity: 0.7
            text: qsTr("Stage, commit and fold as usual. Each action is simulated and listed here; "
                       + "the repository stays untouched until you execute the plan.")
        }

        ListView {
            id: entries

            objectName: "planList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: pane.repo.plan.count > 0
            clip: true
            spacing: 1
            model: pane.repo.plan
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: entry

                required property int index
                required property int entryId
                required property string title
                required property string log
                required property string error

                property bool expanded: false

                width: ListView.view.width
                implicitHeight: content.implicitHeight + 8
                color: error !== "" ? Qt.rgba(0.85, 0.2, 0.2, 0.15)
                                    : index % 2 ? pane.palette.alternateBase : pane.palette.base

                ColumnLayout {
                    id: content

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 4
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true

                        IconButton {
                            iconName: entry.expanded ? "chevron-down" : "chevron-right"
                            onClicked: entry.expanded = !entry.expanded
                        }
                        Label {
                            Layout.fillWidth: true
                            text: `${entry.index + 1}. ${entry.title}`
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            TapHandler {
                                onTapped: entry.expanded = !entry.expanded
                            }
                        }
                        IconButton {
                            iconName: "x"
                            enabled: !pane.repo.writing
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Remove from the plan")
                            onClicked: pane.repo.removePlanEntry(entry.entryId)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 8
                        visible: entry.error !== ""
                        text: entry.error
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: "#d62728"
                    }
                    TextArea {
                        Layout.fillWidth: true
                        visible: entry.expanded
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        textFormat: TextEdit.PlainText
                        font: pane.monoFont
                        text: entry.log
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: pane.palette.mid
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 6
            spacing: 6

            Label {
                Layout.fillWidth: true
                visible: pane.repo.plan.count > 0
                text: qsTr("Executing runs:")
                opacity: 0.7
            }
            Label {
                Layout.fillWidth: true
                visible: pane.repo.plan.count > 0
                text: pane.repo.plan.summary
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                font.family: pane.monoFont.family
                font.pointSize: pane.monoFont.pointSize - 1
            }
            RowLayout {
                Layout.fillWidth: true

                Button {
                    icon.source: Icons.source("x")
                    icon.color: palette.buttonText
                    icon.width: 16
                    icon.height: 16
                    text: qsTr("Discard")
                    enabled: !pane.repo.writing
                    onClicked: pane.discardRequested()
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    icon.source: Icons.source("check")
                    icon.color: palette.buttonText
                    icon.width: 16
                    icon.height: 16
                    objectName: "executePlanButton"
                    text: qsTr("Execute")
                    highlighted: true
                    enabled: pane.repo.plan.executable && !pane.repo.writing
                    onClicked: pane.repo.executePlan()
                }
            }
        }
    }
}
