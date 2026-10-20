pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

// A file through its history, like Perforce's Time-lapse View: drag the
// slider through the revisions; each line shows the revision that last
// changed it, shaded by age, with this revision's changes highlighted.
ApplicationWindow {
    id: window

    required property string workDir
    required property string path
    required property font monoFont

    readonly property var current: lapse.revision >= 0 && lapse.revision < lapse.revisionCount
                                   ? lapse.revisions[lapse.revision] : null
    readonly property color ageColor: palette.highlight

    function step(delta: int) {
        lapse.revision = Math.max(0, Math.min(lapse.revisionCount - 1, lapse.revision + delta))
    }

    width: 1100
    height: 800
    visible: true
    title: qsTr("Time-lapse: %1").arg(path)

    Timelapse {
        id: lapse
        workDir: window.workDir
        path: window.path
    }

    Timer {
        id: player
        interval: 800
        repeat: true
        onTriggered: {
            if (lapse.revision >= lapse.revisionCount - 1)
                stop()
            else
                window.step(1)
        }
    }

    Shortcut {
        sequence: "Left"
        onActivated: window.step(-1)
    }
    Shortcut {
        sequence: "Right"
        onActivated: window.step(1)
    }
    Shortcut {
        sequence: "Home"
        onActivated: lapse.revision = 0
    }
    Shortcut {
        sequence: "End"
        onActivated: lapse.revision = lapse.revisionCount - 1
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6

            IconButton {
                iconName: "skip-back"
                enabled: lapse.revision > 0
                onClicked: lapse.revision = 0
                ToolTip.visible: hovered
                ToolTip.text: qsTr("First revision (Home)")
            }
            IconButton {
                iconName: "step-back"
                enabled: lapse.revision > 0
                onClicked: window.step(-1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Previous revision (Left)")
            }
            IconButton {
                iconName: player.running ? "pause" : "play"
                enabled: lapse.revisionCount > 1
                onClicked: {
                    if (player.running) {
                        player.stop()
                    } else {
                        if (lapse.revision >= lapse.revisionCount - 1)
                            lapse.revision = 0
                        player.start()
                    }
                }
                ToolTip.visible: hovered
                ToolTip.text: player.running ? qsTr("Pause") : qsTr("Play through the revisions")
            }
            IconButton {
                iconName: "step-forward"
                enabled: lapse.revision < lapse.revisionCount - 1
                onClicked: window.step(1)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Next revision (Right)")
            }
            IconButton {
                iconName: "skip-forward"
                enabled: lapse.revision < lapse.revisionCount - 1
                onClicked: lapse.revision = lapse.revisionCount - 1
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Newest revision (End)")
            }
            Label {
                text: lapse.revisionCount > 0 ? qsTr("Revision %1 of %2").arg(lapse.revision + 1).arg(lapse.revisionCount)
                                              : ""
            }
            BusyIndicator {
                implicitWidth: 22
                implicitHeight: 22
                padding: 1
                running: lapse.busy
                visible: running
            }
            Item {
                Layout.fillWidth: true
            }
            CheckBox {
                text: qsTr("Show deleted lines")
                checked: lapse.showDeleted
                onToggled: lapse.showDeleted = checked
            }
            // Legend for the age shading.
            Label {
                text: qsTr("older")
                opacity: 0.7
            }
            Rectangle {
                implicitWidth: 80
                implicitHeight: 10
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Qt.alpha(window.ageColor, 0.1) }
                    GradientStop { position: 1; color: Qt.alpha(window.ageColor, 0.95) }
                }
            }
            Label {
                text: qsTr("newer")
                opacity: 0.7
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The revisions as ticks along a slider, oldest on the left.
        Pane {
            Layout.fillWidth: true
            padding: 8

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                Item {
                    Layout.fillWidth: true
                    implicitHeight: 34

                    Repeater {
                        model: lapse.revisionCount
                        delegate: Rectangle {
                            required property int index
                            x: slider.leftPadding + slider.availableWidth * (lapse.revisionCount > 1
                                   ? index / (lapse.revisionCount - 1) : 1) - width / 2
                            y: 0
                            width: index === lapse.revision ? 3 : 1
                            height: 10
                            color: index === lapse.revision ? window.palette.highlight : window.palette.mid
                        }
                    }
                    Slider {
                        id: slider
                        objectName: "revisionSlider"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        from: 0
                        to: Math.max(1, lapse.revisionCount - 1)
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        enabled: lapse.revisionCount > 1
                        value: lapse.revision
                        onMoved: lapse.revision = Math.round(value)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: window.current !== null

                    Label {
                        text: window.current ? window.current.shortSha : ""
                        font.family: window.monoFont.family
                        font.bold: true
                    }
                    Label {
                        text: window.current ? `${window.current.author} · ${window.current.date}` : ""
                        opacity: 0.8
                    }
                    Label {
                        Layout.fillWidth: true
                        text: window.current ? window.current.subject : ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.bold: true
                    }
                    Label {
                        visible: window.current !== null && window.current.path !== window.path
                        text: window.current ? qsTr("as %1").arg(window.current.path) : ""
                        opacity: 0.7
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.margins: 12
            visible: lapse.error !== ""
            text: lapse.error
            wrapMode: Text.Wrap
            color: "#d62728"
        }

        ListView {
            id: lines

            objectName: "timelapseLines"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: lapse
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            flickableDirection: Flickable.AutoFlickIfNeeded
            contentWidth: Math.max(width, 1600)
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}

            FontMetrics {
                id: metrics
                font: window.monoFont
            }

            delegate: Rectangle {
                id: line

                required property bool deleted
                required property string text
                required property int lineNo
                required property int revision
                required property string shortSha
                required property string author
                required property real age
                required property bool changedHere

                width: lines.contentWidth
                height: metrics.height + 4
                color: line.deleted ? Qt.rgba(0.85, 0.25, 0.25, 0.18)
                     : line.changedHere ? Qt.rgba(0.95, 0.75, 0.1, 0.28)
                     : Qt.alpha(window.ageColor, 0.06 * line.age)

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    // Age bar: the newer the revision, the stronger.
                    Rectangle {
                        width: 6
                        height: line.height
                        color: Qt.alpha(window.ageColor, 0.1 + 0.85 * line.age)
                    }
                    // Blame: click to go to that revision.
                    Label {
                        width: 210
                        leftPadding: 6
                        text: line.deleted ? qsTr("deleted here") : `${line.shortSha}  ${line.author}`
                        font.family: window.monoFont.family
                        font.pointSize: window.monoFont.pointSize - 1
                        elide: Text.ElideRight
                        opacity: line.revision === lapse.revision ? 1 : 0.65
                        color: blameHover.hovered && line.revision >= 0 ? window.palette.link : window.palette.text

                        HoverHandler {
                            id: blameHover
                            cursorShape: line.revision >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
                        }
                        TapHandler {
                            enabled: line.revision >= 0
                            onTapped: lapse.revision = line.revision
                        }
                    }
                    Label {
                        width: 50
                        rightPadding: 8
                        horizontalAlignment: Text.AlignRight
                        text: line.deleted ? "" : line.lineNo
                        font: window.monoFont
                        color: window.palette.placeholderText
                    }
                    Text {
                        text: line.text.replace(/\t/g, "    ")
                        textFormat: Text.PlainText
                        font.family: window.monoFont.family
                        font.pointSize: window.monoFont.pointSize
                        font.strikeout: line.deleted
                        color: window.palette.text
                    }
                }
            }
        }
    }
}
