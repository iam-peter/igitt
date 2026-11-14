pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// A file through its history, like Perforce's Time-lapse View: pick a
// revision on the timeline; each line shows the revision that last
// changed it, shaded by age, with the lines this revision added, changed
// and removed in green, orange and red, as in its flow.
ApplicationWindow {
    id: window

    required property string workDir
    required property string path
    required property font monoFont

    readonly property var current: lapse.revision >= 0 && lapse.revision < lapse.revisionCount
                                   ? lapse.revisions[lapse.revision] : null
    readonly property color ageColor: Styles.TokenInterface.semantics.info_default

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
                id: showFlow
                objectName: "showFlow"
                styles: [Styles.ToggleStyle.small]
                text: qsTr("Flow")
                onToggled: {
                    if (checked)
                        lapse.loadFlow()
                }
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Show how the lines flow from revision to revision: kept, removed in red, added in green")
            }
            CheckBox {
                id: evenSpacing
                styles: [Styles.ToggleStyle.small]
                text: qsTr("Even spacing")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Space the revisions evenly instead of by date")
            }
            CheckBox {
                styles: [Styles.ToggleStyle.small]
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

        // The revisions along a timeline, oldest on the left.
        Pane {
            Layout.fillWidth: true
            padding: 8

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                RevisionTimeline {
                    id: timeline

                    objectName: "revisionTimeline"
                    Layout.fillWidth: true
                    implicitHeight: 54
                    times: lapse.revisions.map(r => r.time)
                    authors: lapse.revisions.map(r => r.author)
                    current: lapse.revision
                    evenSpacing: evenSpacing.checked
                    lineColor: Styles.TokenInterface.semantics.stroke_subtle
                    highlightColor: Styles.TokenInterface.semantics.primary_default
                    textColor: Styles.TokenInterface.semantics.text_muted
                    onRevisionRequested: revision => lapse.revision = revision

                    ToolTip {
                        readonly property var revision: timeline.hoveredRevision >= 0
                                                        ? lapse.revisions[timeline.hoveredRevision] : null
                        visible: revision !== null
                        x: timeline.xOf(timeline.hoveredRevision) - width / 2
                        y: -height - 4
                        text: revision ? `${revision.shortSha} · ${revision.author} · ${revision.date}\n${revision.subject}`
                                       : ""
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

        // The file's history as a Sankey diagram: a node per revision, as
        // long as the file then, and the lines flowing between them
        Pane {
            id: flowPane

            Layout.fillWidth: true
            Layout.preferredHeight: 300
            visible: showFlow.checked
            padding: 8

            // Keeps the node of the revision shown in sight
            function follow() {
                const x = flow.xOf(lapse.revision)
                if (x < 40)
                    flow.scroll = Math.max(0, flow.scroll + x - 40)
                else if (x > flow.width - 40)
                    flow.scroll = Math.min(flow.maxScroll, flow.scroll + x - flow.width + 40)
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                FileFlowChart {
                    id: flow

                    readonly property real maxScroll: Math.max(0, contentWidth - width)

                    objectName: "fileFlow"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    steps: lapse.flow
                    labels: lapse.revisions.map(r => r.shortSha.substring(0, 7))
                    current: lapse.revision
                    // Spread over the width, until zoomed by hand
                    spacing: Math.max(48, fittingSpacing)
                    keptColor: Styles.TokenInterface.semantics.text_muted
                    addedColor: Styles.TokenInterface.semantics.success_default
                    removedColor: Styles.TokenInterface.semantics.danger_default
                    changedColor: Styles.TokenInterface.semantics.warning_default
                    nodeColor: Styles.TokenInterface.semantics.text_muted
                    currentColor: window.ageColor
                    textColor: Styles.TokenInterface.semantics.text_muted
                    onRevisionRequested: revision => lapse.revision = revision
                    // The lines in sight in the code, on the current node
                    viewStart: lines.lineSpan.start
                    viewEnd: lines.lineSpan.end
                    onLineRequested: line => lines.center(lapse.rowOf(line))
                    // At first, the newest revisions
                    onStepsChanged: scroll = maxScroll

                    // The wheel scrolls through the revisions, with Ctrl
                    // it zooms, keeping the revision under the pointer
                    WheelHandler {
                        target: null
                        onWheel: event => {
                            const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                            if (event.modifiers & Qt.ControlModifier) {
                                const at = (event.x + flow.scroll) / flow.spacing
                                flow.spacing = flow.spacing * (delta > 0 ? 1.15 : 1 / 1.15)
                                flow.scroll = Math.max(0, Math.min(flow.maxScroll, at * flow.spacing - event.x))
                            } else {
                                flow.scroll = Math.max(0, Math.min(flow.maxScroll, flow.scroll - delta))
                            }
                        }
                    }
                    ToolTip {
                        readonly property var revision: flow.hoveredRevision >= 0
                                                        ? lapse.revisions[flow.hoveredRevision] : null
                        readonly property var step: flow.hoveredRevision >= 0 ? lapse.flow[flow.hoveredRevision] : null
                        visible: revision !== null && step !== undefined && step !== null
                        x: flow.xOf(flow.hoveredRevision) + 12
                        y: 0
                        text: revision && step
                              ? `${revision.shortSha} · ${revision.author} · ${revision.date}\n${revision.subject}\n`
                                + qsTr("+%1 −%2, %n line(s)", "", step.lines).arg(step.added).arg(step.removed)
                              : ""
                    }
                }
                ScrollBar {
                    Layout.fillWidth: true
                    orientation: Qt.Horizontal
                    policy: flow.contentWidth > flow.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    size: flow.width / Math.max(1, flow.contentWidth)
                    position: flow.scroll / Math.max(1, flow.contentWidth)
                    onPositionChanged: {
                        if (active)
                            flow.scroll = Math.max(0, Math.min(flow.maxScroll, position * flow.contentWidth))
                    }
                }
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: showFlow.checked && lapse.flow.length === 0 && lapse.revisionCount > 0
                visible: running
            }
            Connections {
                target: lapse
                function onRevisionChanged() {
                    flowPane.follow()
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.margins: 12
            visible: lapse.error !== ""
            text: lapse.error
            wrapMode: Text.Wrap
            color: Styles.TokenInterface.semantics.danger_default
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ListView {
                id: lines

                readonly property real rowHeight: metrics.height + 4
                // The rows in sight, fractional as it scrolls, and the lines of
                // the file they are, which differ by the deleted lines shown
                readonly property real firstRow: Math.max(0, (contentY - originY) / rowHeight)
                readonly property real rowsInSight: Math.min(height / rowHeight, Math.max(0, count - firstRow))
                // (rows read again with every revision, as the lines change)
                readonly property var lineSpan: ({rows: lapse.changes.length, start: lapse.lineAt(firstRow),
                                                  end: lapse.lineAt(firstRow + rowsInSight)})

                // Scrolls row `row` to the middle, as far as it goes
                function center(row: real) {
                    const top = row * rowHeight - height / 2
                    contentY = originY + Math.max(0, Math.min(contentHeight - height, top))
                }

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
                    required property int change // LineChange: kept, removed, added, changed

                    width: lines.contentWidth
                    height: metrics.height + 4
                    color: [Qt.alpha(window.ageColor, 0.06 * line.age),
                            Styles.TokenInterface.semantics.diff_removed,
                            Styles.TokenInterface.semantics.diff_added,
                            Styles.TokenInterface.semantics.diff_changed][line.change]

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
                            color: blameHover.hovered && line.revision >= 0 ? Styles.TokenInterface.semantics.text_link : Styles.TokenInterface.semantics.text_default

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
                            color: Styles.TokenInterface.semantics.text_muted
                        }
                        Text {
                            text: line.text.replace(/\t/g, "    ")
                            textFormat: Text.PlainText
                            font.family: window.monoFont.family
                            font.pointSize: window.monoFont.pointSize
                            font.strikeout: line.deleted
                            color: Styles.TokenInterface.semantics.text_default
                        }
                    }
                }
            }

            // The whole file beside its lines: what the revision added, changed
            // and removed, and the rows in sight
            ChangeMinimap {
                objectName: "changeMinimap"
                Layout.fillHeight: true
                Layout.preferredWidth: 14
                changes: lapse.changes
                hiddenRemovals: lapse.hiddenRemovals
                firstVisible: lines.firstRow
                visibleCount: lines.rowsInSight
                addedColor: Styles.TokenInterface.semantics.success_default
                removedColor: Styles.TokenInterface.semantics.danger_default
                changedColor: Styles.TokenInterface.semantics.warning_default
                bandColor: Styles.TokenInterface.semantics.text_muted
                onRowRequested: row => lines.center(row)
            }
        }
    }
}
