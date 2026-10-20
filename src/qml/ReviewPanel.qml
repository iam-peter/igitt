pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

// What Gerrit has on the commit on display: its change's messages, the
// inline comments, and the patch sets to compare the commit with.
ColumnLayout {
    id: panel

    required property Repository repo
    required property font monoFont
    // For the open tab's list; it scrolls beyond that.
    property real maxBodyHeight: 240
    property bool expanded: true

    readonly property var review: repo.commitReview
    readonly property bool hasReview: review.number !== undefined

    visible: hasReview
    spacing: 0

    component Body: TextEdit {
        Layout.fillWidth: true
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        color: panel.palette.text
        selectionColor: panel.palette.highlight
        selectedTextColor: panel.palette.highlightedText
    }

    component Caption: Label {
        opacity: 0.7
        elide: Text.ElideRight
    }

    function dateText(date) {
        return date ? Qt.formatDateTime(date, "yyyy-MM-dd hh:mm") : ""
    }

    ToolBar {
        Layout.fillWidth: true

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 2
            anchors.rightMargin: 6

            IconButton {
                objectName: "reviewToggle"
                iconName: panel.expanded ? "chevron-down" : "chevron-right"
                onClicked: panel.expanded = !panel.expanded
            }
            Label {
                font.bold: true
                text: qsTr("Review")
            }
            Label {
                text: panel.hasReview ? `<a href="${panel.review.url}">${qsTr("Change %1").arg(panel.review.number)}</a>` : ""
                textFormat: Text.StyledText
                onLinkActivated: link => Qt.openUrlExternally(link)

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }
            Caption {
                Layout.fillWidth: true
                text: !panel.hasReview ? ""
                    : panel.review.localPatchSet > 0
                      ? qsTr("This commit is patch set %1 of %2").arg(panel.review.localPatchSet).arg(panel.review.patchSet)
                      : qsTr("This commit is none of the %n patch set(s)", "", panel.review.patchSet)
            }
            Tag {
                visible: panel.hasReview && panel.review.unresolved === true
                text: qsTr("unresolved comments")
                tint: "#e08e2b"
            }
            IconButton {
                objectName: "showCommitButton"
                visible: panel.repo.diffSource === Repository.PatchSetDiff
                iconName: "git-commit-horizontal"
                text: qsTr("Show Commit")
                onClicked: panel.repo.showCommit(panel.repo.diffCommit)
            }
        }
    }

    TabBar {
        id: tabs

        Layout.fillWidth: true
        visible: panel.expanded

        TabButton {
            text: qsTr("Comments (%1)").arg(panel.hasReview ? panel.review.comments.length : 0)
            width: implicitWidth
        }
        TabButton {
            objectName: "messagesTab"
            text: qsTr("Messages (%1)").arg(panel.hasReview ? panel.review.messages.length : 0)
            width: implicitWidth
        }
        TabButton {
            objectName: "patchSetsTab"
            text: qsTr("Patch Sets (%1)").arg(panel.hasReview ? panel.review.patchSets.length : 0)
            width: implicitWidth
        }
    }

    StackLayout {
        id: stack

        Layout.fillWidth: true
        Layout.fillHeight: false
        Layout.preferredHeight: Math.min(([comments, messages, patchSets][currentIndex]).contentHeight,
                                         panel.maxBodyHeight)
        visible: panel.expanded
        currentIndex: tabs.currentIndex

        ListView {
            id: comments

            leftMargin: 6
            rightMargin: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.hasReview ? panel.review.comments : []
            spacing: 6
            ScrollBar.vertical: ScrollBar {}
            // The SSH interface igitt asks Gerrit through has no word on
            // single comments being resolved; say so instead of guessing.
            header: ColumnLayout {
                width: ListView.view.width - 12
                spacing: 2

                Caption {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Layout.bottomMargin: 4
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                    text: {
                        if (!panel.hasReview)
                            return ""
                        const resolved = panel.review.unresolved === true ? qsTr("The change has unresolved comments.")
                                       : panel.review.unresolved === false ? qsTr("The change has no unresolved comments.")
                                       : qsTr("Whether it has unresolved comments is not known.")
                        const lines = [qsTr("Gerrit's SSH interface does not tell which comments are resolved.")
                                       + " " + resolved]
                        if (panel.review.localPatchSet === 0 && panel.repo.diffPatchSet === 0)
                            lines.push(qsTr("Comments show on the lines of the diff only for a patch set the diff compares."))
                        return lines.join("\n")
                    }
                }
            }
            delegate: ColumnLayout {
                id: comment

                required property var modelData

                width: ListView.view.width - 12
                spacing: 1

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: comment.modelData.file + (comment.modelData.line > 0 ? ":" + comment.modelData.line : "")
                        font.family: panel.monoFont.family
                        font.bold: true
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: comment.width * 0.6
                    }
                    Caption {
                        Layout.fillWidth: true
                        text: [comment.modelData.author, qsTr("patch set %1").arg(comment.modelData.patchSet)]
                              .concat(comment.modelData.marked ? [qsTr("in the diff")] : []).join(" · ")
                    }
                }
                Body {
                    text: comment.modelData.text
                }
            }
        }

        ListView {
            id: messages

            leftMargin: 6
            rightMargin: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.hasReview ? panel.review.messages : []
            spacing: 8
            ScrollBar.vertical: ScrollBar {}
            topMargin: 6
            bottomMargin: 6
            // Newest at the bottom, as in Gerrit; start there.
            onCountChanged: Qt.callLater(positionViewAtEnd)
            delegate: ColumnLayout {
                id: message

                required property var modelData

                width: ListView.view.width - 12
                spacing: 1

                RowLayout {
                    Label {
                        font.bold: true
                        text: message.modelData.author || qsTr("Gerrit")
                    }
                    Caption {
                        text: panel.dateText(message.modelData.date)
                    }
                }
                Body {
                    text: message.modelData.text
                }
            }
        }

        ListView {
            id: patchSets

            leftMargin: 6
            rightMargin: 6
            objectName: "patchSetList"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.hasReview ? panel.review.patchSets : []
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: patchSet

                required property var modelData
                readonly property bool shown: panel.repo.diffPatchSet === modelData.number

                width: ListView.view.width - 12
                spacing: 10

                Label {
                    Layout.minimumWidth: 40
                    font.bold: true
                    text: qsTr("PS %1").arg(patchSet.modelData.number)
                }
                Label {
                    Layout.preferredWidth: 150
                    elide: Text.ElideRight
                    text: patchSet.modelData.kindText
                }
                Caption {
                    Layout.preferredWidth: 150
                    text: patchSet.modelData.uploader
                }
                Caption {
                    text: panel.dateText(patchSet.modelData.date)
                }
                Caption {
                    visible: patchSet.modelData.comments > 0
                    text: qsTr("%n comment(s)", "", patchSet.modelData.comments)
                }
                Tag {
                    visible: patchSet.modelData.local
                    text: qsTr("this commit")
                    tint: "#59a14f"
                }
                Tag {
                    visible: patchSet.modelData.current
                    text: qsTr("current")
                    tint: "#4e79a7"
                }
                Item {
                    Layout.fillWidth: true
                }
                IconButton {
                    objectName: `comparePatchSet${patchSet.modelData.number}`
                    iconName: "git-compare"
                    text: patchSet.shown ? qsTr("Compared") : qsTr("Compare")
                    enabled: !patchSet.modelData.local && !patchSet.shown
                    onClicked: panel.repo.comparePatchSet(patchSet.modelData.number)
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Show how this commit differs from patch set %1, fetching it if needed")
                                      .arg(patchSet.modelData.number)
                }
            }
        }
    }
}
