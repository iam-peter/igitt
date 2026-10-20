pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

// Gerrit push options for one push for review: topic, work in progress or
// ready, reviewers and hashtags. The topic starts as the one the branch was
// last pushed with.
Popup {
    id: popup

    required property Repository repo

    readonly property var options: ({
        topic: topicField.text,
        state: stateBox.currentValue,
        reviewers: reviewersField.text,
        hashtags: hashtagsField.text,
    })
    readonly property var refspec: repo.reviewRefspec(options)

    objectName: "reviewOptions"
    padding: 12
    focus: true
    onAboutToShow: {
        topicField.text = repo.reviewTopic
        topicField.forceActiveFocus()
    }

    function push() {
        if (popup.refspec.error !== "")
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

            Label { text: qsTr("Topic") }
            TextField {
                id: topicField
                objectName: "reviewTopicField"
                Layout.preferredWidth: 280
                placeholderText: qsTr("none")
                onAccepted: popup.push()
            }
            Label { text: qsTr("State") }
            ComboBox {
                id: stateBox
                objectName: "reviewStateBox"
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "value"
                model: [
                    {value: "", text: qsTr("Leave as it is")},
                    {value: "wip", text: qsTr("Work in progress")},
                    {value: "ready", text: qsTr("Ready for review")},
                ]
            }
            Label { text: qsTr("Reviewers") }
            TextField {
                id: reviewersField
                objectName: "reviewReviewersField"
                Layout.fillWidth: true
                placeholderText: qsTr("a@example.org, b@example.org")
                onAccepted: popup.push()
            }
            Label { text: qsTr("Hashtags") }
            TextField {
                id: hashtagsField
                objectName: "reviewHashtagsField"
                Layout.fillWidth: true
                placeholderText: qsTr("comma-separated")
                onAccepted: popup.push()
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 360
            wrapMode: Text.WrapAnywhere
            textFormat: Text.PlainText
            text: popup.refspec.error !== "" ? popup.refspec.error : popup.refspec.refspec
            color: popup.refspec.error !== "" ? "#d04040" : palette.text
            opacity: popup.refspec.error !== "" ? 1 : 0.7
            font.family: AppSettings.fonts.codeFamily
        }
        Button {
            objectName: "reviewPushButton"
            Layout.alignment: Qt.AlignRight
            text: qsTr("Push for Review")
            enabled: popup.refspec.error === ""
            onClicked: popup.push()
        }
    }
}
