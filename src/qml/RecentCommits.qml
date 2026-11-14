pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// The commits not pushed yet above the changes, and the pushed one they
// stand on, for context while committing: what the branch holds, and the
// commit the changes would fold into. Up to `maxRows` rows, scrolling for
// more; none while everything is pushed. Clicking one shows it in History.
//
// Selected lines or a file dropped onto a commit not pushed yet fold into
// it: the dragging side calls updateDrop() as the pointer moves and
// finishDrop() where it is let go, which says the commit to fold into.
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

    // Where a fold would go while one is dragged, and why it cannot
    property int dropRow: -1
    property string dropBlocker

    signal commitClicked(string sha)

    function rowAt(scenePos: point): int {
        if (!visible)
            return -1
        const local = list.mapFromItem(null, scenePos.x, scenePos.y)
        if (local.x < 0 || local.y < 0 || local.x >= list.width || local.y >= list.height)
            return -1
        const content = list.contentItem.mapFromItem(null, scenePos.x, scenePos.y)
        return list.indexAt(content.x, content.y)
    }
    // `file`: a whole file, not the lines selected
    function updateDrop(scenePos: point, file: bool) {
        const row = rowAt(scenePos)
        if (row === dropRow)
            return
        const sha = repo.commits.shaAt(row)
        dropBlocker = row < 0 ? ""
                    : row >= unpushed ? qsTr("Pushed: folding into it would need a force push")
                    : file ? repo.foldTargetBlocker(sha) : repo.foldBlocker(sha)
        dropRow = row
    }
    // The commit to fold into, or "" where it was let go elsewhere
    function finishDrop(scenePos: point, file: bool): string {
        updateDrop(scenePos, file)
        const sha = dropRow >= 0 && dropBlocker === "" ? repo.commits.shaAt(dropRow) : ""
        dropRow = -1
        dropBlocker = ""
        return sha
    }

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

    visible: unpushed > 0
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
            readonly property bool dropTarget: index === strip.dropRow
            readonly property bool refused: dropTarget && strip.dropBlocker !== ""

            width: ListView.view.width
            height: 24
            topPadding: 0
            bottomPadding: 0
            onClicked: strip.commitClicked(sha)
            ToolTip.visible: hovered && strip.dropRow < 0
            ToolTip.delay: 600
            ToolTip.text: qsTr("Show it in History; drop selected lines or a file here to fold them in")
            background: Rectangle {
                color: row.refused ? Qt.alpha(Styles.TokenInterface.semantics.danger_default, 0.15)
                     : row.dropTarget ? Qt.alpha(Styles.TokenInterface.semantics.success_default, 0.18)
                     : row.hovered ? Qt.alpha(Styles.TokenInterface.semantics.text_muted, 0.12) : "transparent"
                border.width: row.dropTarget ? 2 : 0
                border.color: row.refused ? Styles.TokenInterface.semantics.danger_default
                                          : Styles.TokenInterface.semantics.success_default
                radius: 3
            }

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
                    text: row.refused ? strip.dropBlocker
                        : row.dropTarget ? qsTr("Fold into this commit")
                        : `${row.commit.author ?? ""} · ${row.commit.date ?? ""}`
                    color: row.refused ? Styles.TokenInterface.semantics.danger_default
                         : row.dropTarget ? Styles.TokenInterface.semantics.success_default
                         : Styles.TokenInterface.semantics.text_muted
                    font.bold: row.dropTarget
                }
            }
        }
    }
}
