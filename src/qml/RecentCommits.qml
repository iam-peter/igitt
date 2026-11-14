pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// The commits not pushed yet above the changes, and the pushed one they
// stand on, for context while committing: what the branch holds, and the
// commit the changes would fold into. Up to `maxRows` rows, scrolling for
// more. Clicking one shows it in History.
Pane {
    id: strip

    required property Repository repo
    property int maxRows: 6
    // The commits not pushed yet, newest first, then the one below them
    property int unpushed: 0
    property int total: 0
    readonly property int shown: Math.min(unpushed + 1, total)
    // Counted up whenever the commits are read again, to read the rows anew
    property int revision: 0

    signal commitClicked(string sha)

    function recount() {
        const commits = repo.commits
        const rows = commits.rowCount()
        let count = 0
        while (count < rows && count < 200 && !repo.isPublished(commits.shaAt(count)))
            ++count
        unpushed = count
        total = rows
        ++revision
    }

    Component.onCompleted: recount()
    Connections {
        target: strip.repo.commits
        function onModelReset() {
            strip.recount()
        }
    }
    Connections {
        target: strip.repo
        function onHeadChanged() {
            strip.recount()
        }
    }

    padding: 0
    background: Rectangle {
        color: Styles.TokenInterface.semantics.background_default
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Styles.TokenInterface.semantics.stroke_subtle
        }
    }

    contentItem: ListView {
        id: list

        objectName: "recentCommits"
        implicitHeight: Math.min(strip.shown, strip.maxRows) * 24 + 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        boundsMovement: Flickable.StopAtBounds
        // The rows from the commits, by index: only as many as shown
        model: strip.shown
        ScrollBar.vertical: ScrollBar {
            policy: strip.shown > strip.maxRows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }

        delegate: ItemDelegate {
            id: row

            required property int index
            readonly property var commit: (strip.revision, strip.repo.commits.rowAt(index))
            readonly property string sha: commit.sha ?? ""
            readonly property var refs: commit.refs ?? []
            readonly property string laneColor: commit.laneColor ?? "gray"
            readonly property bool isHead: commit.isHead ?? false
            readonly property bool published: index >= strip.unpushed

            width: ListView.view.width
            height: 24
            topPadding: 0
            bottomPadding: 0
            onClicked: strip.commitClicked(sha)
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Show it in History")

            contentItem: RowLayout {
                spacing: 8

                Rectangle {
                    implicitWidth: 8
                    implicitHeight: 8
                    radius: 4
                    color: row.laneColor
                    border.width: row.isHead ? 2 : 0
                    border.color: Styles.TokenInterface.semantics.text_default
                }
                Label {
                    text: (row.commit.shortSha ?? "").substring(0, 8)
                    font.family: "monospace"
                    opacity: 0.7
                }
                RefLabel {
                    visible: row.refs.length > 0
                    name: row.refs.length > 0 ? row.refs[0].name : ""
                    kind: row.refs.length > 0 ? row.refs[0].kind : ""
                    laneColor: row.laneColor
                    current: row.refs.length > 0 && row.refs[0].current
                    remotes: row.refs.length > 0 ? row.refs[0].remotes : []
                }
                Tag {
                    visible: !row.published
                    text: qsTr("not pushed")
                    tint: Styles.TokenInterface.semantics.info_default
                }
                Tag {
                    visible: row.sha !== "" && row.sha === strip.repo.suggestedFoldTarget
                    text: qsTr("fold here")
                    tint: Styles.TokenInterface.semantics.success_default
                }
                Label {
                    Layout.fillWidth: true
                    text: row.commit.subject ?? ""
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.bold: row.isHead
                    opacity: row.published ? 0.7 : 1
                }
                Label {
                    text: `${row.commit.author ?? ""} · ${row.commit.date ?? ""}`
                    color: Styles.TokenInterface.semantics.text_muted
                }
            }
        }
    }
}
