pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

// Options for one push, as the review host takes them. Gerrit: topic, work
// in progress or ready, reviewers and hashtags; the topic starts as the one
// the branch was last pushed with. GitLab: merge request push options.
// GitHub: a pull request opened with gh after the push.
Popup {
    id: popup

    required property Repository repo

    readonly property bool gerrit: repo.reviewHost === "gerrit"
    readonly property bool github: repo.reviewHost === "github"
    readonly property bool gitlab: repo.reviewHost === "gitlab"
    readonly property bool hasOpenRequest: repo.branchReview.state === "open"
    // GitHub's options are for a new pull request; GitLab's push options
    // also update an open merge request.
    readonly property bool requestOptions: !github || createBox.checked
    readonly property var options: gerrit ? {
        topic: topicField.text,
        state: stateBox.currentValue,
        reviewers: reviewersField.text,
        hashtags: hashtagsField.text,
    } : {
        create: createBox.checked,
        target: targetField.text,
        title: titleField.text,
        state: draftBox.checked ? "wip" : "",
        reviewers: github ? reviewersField.text : "",
        labels: labelsField.text,
        removeSourceBranch: gitlab && removeBranchBox.checked,
    }
    readonly property var preview: repo.pushPreview(options)

    objectName: "reviewOptions"
    padding: 12
    focus: true
    onAboutToShow: {
        topicField.text = repo.reviewTopic
        // An open pull or merge request is updated by pushing.
        createBox.checked = !hasOpenRequest
        if (gerrit)
            topicField.forceActiveFocus()
        else
            titleField.forceActiveFocus()
    }

    // Reviewers, hashtags and the like belong to one project.
    Connections {
        target: popup.repo
        function onPathChanged() {
            popup.close()
            for (const field of [targetField, titleField, reviewersField, hashtagsField, labelsField])
                field.clear()
            stateBox.currentIndex = 0
            draftBox.checked = false
            removeBranchBox.checked = false
        }
    }

    function push() {
        if (popup.preview.error !== "")
            return
        popup.repo.push(false, popup.options)
        popup.close()
    }

    ColumnLayout {
        spacing: 8

        GridLayout {
            columns: 2
            columnSpacing: 8
            rowSpacing: 6

            // Gerrit
            Label { visible: popup.gerrit; text: qsTr("Topic") }
            TextField {
                id: topicField
                objectName: "reviewTopicField"
                visible: popup.gerrit
                Layout.preferredWidth: 280
                placeholderText: qsTr("none")
                onAccepted: popup.push()
            }
            Label { visible: popup.gerrit; text: qsTr("State") }
            ComboBox {
                id: stateBox
                objectName: "reviewStateBox"
                visible: popup.gerrit
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [
                    {value: "", text: qsTr("Leave as it is")},
                    {value: "wip", text: qsTr("Work in progress")},
                    {value: "ready", text: qsTr("Ready for review")},
                ]
            }

            // GitHub and GitLab
            Item { visible: !popup.gerrit; implicitWidth: 1 }
            CheckBox {
                id: createBox
                objectName: "reviewCreateBox"
                visible: !popup.gerrit
                enabled: !popup.hasOpenRequest
                padding: 0
                text: popup.hasOpenRequest
                      ? qsTr("%1 is open; pushing updates it").arg(popup.repo.branchReview.label)
                      : popup.github ? qsTr("Open a pull request") : qsTr("Open a merge request")
            }
            Label { visible: !popup.gerrit; text: qsTr("Into") }
            TextField {
                id: targetField
                objectName: "reviewTargetField"
                enabled: popup.requestOptions
                visible: !popup.gerrit
                Layout.preferredWidth: 280
                placeholderText: qsTr("the default branch")
                onAccepted: popup.push()
            }
            Label { visible: !popup.gerrit; text: qsTr("Title") }
            TextField {
                id: titleField
                objectName: "reviewTitleField"
                enabled: popup.requestOptions
                visible: !popup.gerrit
                Layout.fillWidth: true
                placeholderText: qsTr("from the commits")
                onAccepted: popup.push()
            }
            Item { visible: !popup.gerrit; implicitWidth: 1 }
            CheckBox {
                id: draftBox
                objectName: "reviewDraftBox"
                enabled: popup.requestOptions
                visible: !popup.gerrit
                padding: 0
                text: qsTr("Draft")
            }

            Label { visible: popup.gerrit || popup.github; text: qsTr("Reviewers") }
            TextField {
                id: reviewersField
                objectName: "reviewReviewersField"
                enabled: popup.gerrit || popup.requestOptions
                visible: popup.gerrit || popup.github
                Layout.fillWidth: true
                placeholderText: popup.github ? qsTr("user names, comma-separated") : qsTr("a@example.org, b@example.org")
                onAccepted: popup.push()
            }
            Label { visible: popup.gerrit; text: qsTr("Hashtags") }
            TextField {
                id: hashtagsField
                objectName: "reviewHashtagsField"
                visible: popup.gerrit
                Layout.fillWidth: true
                placeholderText: qsTr("comma-separated")
                onAccepted: popup.push()
            }
            Label { visible: !popup.gerrit; text: qsTr("Labels") }
            TextField {
                id: labelsField
                objectName: "reviewLabelsField"
                enabled: popup.requestOptions
                visible: !popup.gerrit
                Layout.fillWidth: true
                placeholderText: qsTr("comma-separated")
                onAccepted: popup.push()
            }
            Item { visible: popup.gitlab; implicitWidth: 1 }
            CheckBox {
                id: removeBranchBox
                objectName: "reviewRemoveBranchBox"
                visible: popup.gitlab
                padding: 0
                text: qsTr("Delete the branch once merged")
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 360
            wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
            text: popup.preview.error !== "" ? popup.preview.error : popup.preview.text
            color: popup.preview.error !== "" ? "#d04040" : palette.text
            opacity: popup.preview.error !== "" ? 1 : 0.7
            font.family: AppSettings.fonts.codeFamily
        }
        Button {
            objectName: "reviewPushButton"
            Layout.alignment: Qt.AlignRight
            text: popup.gerrit ? qsTr("Push for Review") : qsTr("Push")
            enabled: popup.preview.error === ""
            onClicked: popup.push()
        }
    }
}
