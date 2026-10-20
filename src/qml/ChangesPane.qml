pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

Pane {
    id: pane

    required property Repository repo

    padding: 0

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        FileList {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 80
            repo: pane.repo
            staged: false
            title: qsTr("Unstaged Changes")
        }

        FileList {
            SplitView.preferredHeight: 160
            SplitView.minimumHeight: 80
            repo: pane.repo
            staged: true
            title: qsTr("Staged Changes")
        }

        ColumnLayout {
            SplitView.preferredHeight: 170
            SplitView.minimumHeight: 110
            spacing: 4

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 4

                TextArea {
                    id: message
                    placeholderText: qsTr("Commit message")
                    font.family: "monospace"
                    wrapMode: TextEdit.NoWrap
                    selectByMouse: true

                    Keys.onPressed: event => {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                && (event.modifiers & Qt.ControlModifier)) {
                            commitButton.clicked()
                            event.accepted = true
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                Layout.rightMargin: 4
                Layout.bottomMargin: 4

                CheckBox {
                    id: amend
                    text: qsTr("Amend last commit")
                    enabled: pane.repo.headSha.length > 0
                    onToggled: {
                        if (checked && message.text.trim().length === 0)
                            message.text = pane.repo.headMessage()
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Button {
                    id: commitButton
                    text: amend.checked ? qsTr("Amend") : qsTr("Commit")
                    enabled: message.text.trim().length > 0
                             && (pane.repo.staged.count > 0 || amend.checked)
                    onClicked: {
                        if (!enabled)
                            return
                        if (pane.repo.commit(message.text, amend.checked)) {
                            message.clear()
                            amend.checked = false
                        }
                    }
                }
            }
        }
    }
}
