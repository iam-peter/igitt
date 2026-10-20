pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

// The merge tool, after GitKraken's: both sides next to each other, a
// checkbox on each conflict to take its lines, and the output below,
// which can also be edited by hand before saving.
Pane {
    id: view

    required property Repository repo
    required property string path
    required property font monoFont

    readonly property color oursColor: "#4e79a7"
    readonly property color theirsColor: "#e08e2b"
    property int currentConflict: 0
    // The output was edited by hand; picking a side rebuilds it.
    property bool edited: false

    function chunkOfConflict(conflict: int): int {
        const chunks = resolver.chunks
        for (let i = 0; i < chunks.length; ++i) {
            if (chunks[i].conflictIndex === conflict)
                return i
        }
        return -1
    }

    function goTo(conflict: int) {
        if (resolver.conflictCount === 0)
            return
        currentConflict = (conflict + resolver.conflictCount) % resolver.conflictCount
        const chunk = chunkOfConflict(currentConflict)
        oursPane.list.positionViewAtIndex(chunk, ListView.Center)
        theirsPane.list.positionViewAtIndex(chunk, ListView.Center)
    }

    function save() {
        if (outputArea.text.includes("<<<<<<<") || outputArea.text.includes(">>>>>>>"))
            confirmMarkers.open()
        else
            repo.resolveConflict(path, outputArea.text)
    }

    padding: 0

    ConflictResolver {
        id: resolver
        objectName: "conflictResolver"
        workDir: view.repo.path
        path: view.path
        onLoaded: view.currentConflict = 0
        onChoicesChanged: {
            outputArea.text = resolver.output
            view.edited = false
        }
    }

    Dialog {
        id: confirmMarkers
        anchors.centerIn: parent
        modal: true
        title: qsTr("Conflict markers left")
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: view.repo.resolveConflict(view.path, outputArea.text)

        Label {
            text: qsTr("The output still contains conflict markers. Save it as it is?")
        }
    }

    component SidePane: ColumnLayout {
        id: side

        required property bool theirs
        property alias list: chunkList

        readonly property color tint: theirs ? view.theirsColor : view.oursColor

        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: sideLabel.implicitHeight + 10
            color: Qt.alpha(side.tint, 0.2)

            Label {
                id: sideLabel
                anchors.verticalCenter: parent.verticalCenter
                x: 8
                width: parent.width - 16
                text: side.theirs ? resolver.theirsLabel : resolver.oursLabel
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.bold: true
            }
        }
        ListView {
            id: chunkList

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: resolver.chunks
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: chunk

                required property var modelData
                required property int index

                readonly property int conflict: modelData.conflictIndex
                readonly property bool taken: conflict >= 0 && resolver.choices.length > conflict
                                              && resolver.choices[conflict][side.theirs ? "theirs" : "ours"]

                width: ListView.view.width
                implicitHeight: content.implicitHeight
                color: chunk.conflict < 0 ? "transparent"
                     : Qt.alpha(side.tint, chunk.taken ? 0.28 : 0.1)
                border.width: chunk.conflict === view.currentConflict ? 2 : 0
                border.color: side.tint

                Column {
                    id: content
                    width: parent.width

                    CheckBox {
                        objectName: side.theirs ? "takeTheirs" : "takeOurs"
                        visible: chunk.conflict >= 0
                        height: visible ? implicitHeight : 0
                        text: qsTr("Conflict %1: take these lines").arg(chunk.conflict + 1)
                        checked: chunk.taken
                        onToggled: {
                            view.currentConflict = chunk.conflict
                            resolver.choose(chunk.conflict, side.theirs, checked)
                        }
                    }
                    Text {
                        width: parent.width
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 1
                        bottomPadding: 1
                        text: chunk.conflict < 0 ? chunk.modelData.text
                            : (side.theirs ? chunk.modelData.theirs : chunk.modelData.ours) || qsTr("(no lines)")
                        textFormat: Text.PlainText
                        font: view.monoFont
                        color: view.palette.text
                        opacity: chunk.conflict < 0 ? 0.6 : 1
                        wrapMode: Text.NoWrap
                        clip: true
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6

                Label {
                    text: (resolver.operation !== "" ? resolver.operation + " · " : "") + view.path
                    font.bold: true
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: 320
                }
                Label {
                    visible: resolver.conflictCount > 0
                    text: qsTr("Conflict %1 of %2").arg(view.currentConflict + 1).arg(resolver.conflictCount)
                          + (resolver.unresolvedCount > 0 ? qsTr(" · %n unresolved", "", resolver.unresolvedCount)
                                                          : qsTr(" · all picked"))
                    opacity: 0.8
                }
                ToolButton {
                    text: "▲"
                    enabled: resolver.conflictCount > 1
                    onClicked: view.goTo(view.currentConflict - 1)
                }
                ToolButton {
                    text: "▼"
                    enabled: resolver.conflictCount > 1
                    onClicked: view.goTo(view.currentConflict + 1)
                }
                Item {
                    Layout.fillWidth: true
                }
                ToolButton {
                    visible: resolver.conflictCount > 0
                    text: qsTr("Take All Ours")
                    onClicked: resolver.takeAll(false)
                }
                ToolButton {
                    visible: resolver.conflictCount > 0
                    text: qsTr("Take All Theirs")
                    onClicked: resolver.takeAll(true)
                }
                Button {
                    objectName: "saveResolution"
                    visible: resolver.error === ""
                    text: qsTr("Save and Mark Resolved")
                    highlighted: true
                    enabled: !resolver.busy && !view.repo.writing
                    onClicked: view.save()
                }
            }
        }

        // Conflicts that can't be merged line by line: pick a side.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            visible: resolver.error !== "" && !resolver.busy
            spacing: 12

            Label {
                text: resolver.error
                font.bold: true
            }
            RowLayout {
                Button {
                    text: resolver.deletedInOurs ? qsTr("Keep Deleted (Ours)") : qsTr("Keep Ours")
                    onClicked: view.repo.resolveWithSide(view.path, false)
                }
                Button {
                    text: resolver.deletedInTheirs ? qsTr("Keep Deleted (Theirs)") : qsTr("Keep Theirs")
                    onClicked: view.repo.resolveWithSide(view.path, true)
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: resolver.error === ""
            orientation: Qt.Vertical

            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal

                SidePane {
                    id: oursPane
                    theirs: false
                    SplitView.preferredWidth: view.width / 2
                }
                SidePane {
                    id: theirsPane
                    theirs: true
                    SplitView.fillWidth: true
                }
            }

            ColumnLayout {
                SplitView.preferredHeight: view.height * 0.4
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: outputLabel.implicitHeight + 10
                    color: view.palette.button

                    Label {
                        id: outputLabel
                        anchors.verticalCenter: parent.verticalCenter
                        x: 8
                        width: parent.width - 16
                        text: view.edited ? qsTr("Output, edited; picking a side rebuilds it")
                                          : qsTr("Output; edit freely before saving")
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextArea {
                        id: outputArea
                        objectName: "resolutionOutput"
                        font: view.monoFont
                        wrapMode: TextEdit.NoWrap
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        onTextEdited: view.edited = true
                    }
                }
            }
        }
    }
}
