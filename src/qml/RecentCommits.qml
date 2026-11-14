pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// The last few commits above the changes, for context while committing:
// what the branch holds, which commits are not pushed yet, and the one the
// changes would fold into. Clicking one shows it in History.
Pane {
    id: strip

    required property Repository repo
    property int rows: 4

    signal commitClicked(string sha)

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
        implicitHeight: Math.min(count, strip.rows) * 24 + 2
        interactive: false
        clip: true
        model: strip.repo.commits

        delegate: ItemDelegate {
            id: row

            required property int index
            required property string sha
            required property string shortSha
            required property string subject
            required property string author
            required property string date
            required property var refs
            required property string laneColor
            required property bool isHead
            readonly property bool published: strip.repo.isPublished(sha)

            width: ListView.view.width
            height: 24
            visible: index < strip.rows
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
                    text: row.shortSha.substring(0, 8)
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
                    visible: row.sha === strip.repo.suggestedFoldTarget
                    text: qsTr("fold here")
                    tint: Styles.TokenInterface.semantics.success_default
                }
                Label {
                    Layout.fillWidth: true
                    text: row.subject
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    font.bold: row.isHead
                }
                Label {
                    text: `${row.author} · ${row.date}`
                    color: Styles.TokenInterface.semantics.text_muted
                }
            }
        }
    }
}
