pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt

Dialog {
    id: dialog

    readonly property var commit: AppSettings.commit
    readonly property var graph: AppSettings.graph

    function setKeys(keys) {
        commit.trailerKeys = keys
    }

    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Close

    contentItem: ScrollView {
        implicitWidth: 520
        implicitHeight: Math.min(form.implicitHeight, 560)
        contentWidth: availableWidth

        ColumnLayout {
            id: form

            width: parent.width
            spacing: 8

            Label {
                text: qsTr("Commit messages")
                font.bold: true
            }
            GridLayout {
                columns: 2
                Label {
                    text: qsTr("Title length limit")
                }
                SpinBox {
                    objectName: "titleLimitSetting"
                    from: 20
                    to: 200
                    value: dialog.commit.titleLimit
                    onValueModified: dialog.commit.titleLimit = value
                }
                Label {
                    text: qsTr("Body line width")
                }
                SpinBox {
                    from: 40
                    to: 200
                    value: dialog.commit.bodyWidth
                    onValueModified: dialog.commit.bodyWidth = value
                }
            }
            CheckBox {
                text: qsTr("Wrap the body at the line width when committing")
                checked: dialog.commit.wrapBody
                onToggled: dialog.commit.wrapBody = checked
            }
            CheckBox {
                text: qsTr("Refuse to commit when the title is too long")
                checked: dialog.commit.enforceTitleLimit
                onToggled: dialog.commit.enforceTitleLimit = checked
            }

            Label {
                Layout.topMargin: 8
                text: qsTr("Trailers offered in the commit form, such as Task-number")
            }
            Repeater {
                model: dialog.commit.trailerKeys
                delegate: RowLayout {
                    id: keyRow

                    required property int index
                    required property string modelData

                    Layout.fillWidth: true

                    TextField {
                        Layout.fillWidth: true
                        text: keyRow.modelData
                        selectByMouse: true
                        onEditingFinished: {
                            const keys = Array.from(dialog.commit.trailerKeys)
                            keys[keyRow.index] = text.trim()
                            dialog.setKeys(keys.filter(key => key !== ""))
                        }
                    }
                    ToolButton {
                        text: "▲"
                        enabled: keyRow.index > 0
                        onClicked: {
                            const keys = Array.from(dialog.commit.trailerKeys)
                            keys.splice(keyRow.index - 1, 0, keys.splice(keyRow.index, 1)[0])
                            dialog.setKeys(keys)
                        }
                    }
                    ToolButton {
                        text: "✕"
                        onClicked: {
                            const keys = Array.from(dialog.commit.trailerKeys)
                            keys.splice(keyRow.index, 1)
                            dialog.setKeys(keys)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true

                TextField {
                    id: newKey
                    objectName: "newTrailerKey"
                    Layout.fillWidth: true
                    placeholderText: qsTr("New trailer key, e.g. Reviewed-by")
                    selectByMouse: true
                    validator: RegularExpressionValidator {
                        regularExpression: /[A-Za-z0-9][A-Za-z0-9-]*/
                    }
                    onAccepted: addKey.clicked()
                }
                Button {
                    id: addKey
                    text: qsTr("Add")
                    enabled: newKey.text !== ""
                    onClicked: {
                        dialog.setKeys(Array.from(dialog.commit.trailerKeys).concat([newKey.text]))
                        newKey.clear()
                    }
                }
                Button {
                    text: qsTr("Defaults")
                    onClicked: dialog.setKeys(Array.from(AppSettings.defaultTrailerKeys))
                }
            }

            Label {
                Layout.topMargin: 12
                text: qsTr("History")
                font.bold: true
            }
            CheckBox {
                text: qsTr("Show remote branches")
                checked: dialog.graph.showRemotes
                onToggled: dialog.graph.showRemotes = checked
            }
            CheckBox {
                text: qsTr("Show tags")
                checked: dialog.graph.showTags
                onToggled: dialog.graph.showTags = checked
            }
            CheckBox {
                text: qsTr("Dim merge commits")
                checked: dialog.graph.dimMerges
                onToggled: dialog.graph.dimMerges = checked
            }
            CheckBox {
                text: qsTr("Show author initials in the graph")
                checked: dialog.graph.showInitials
                onToggled: dialog.graph.showInitials = checked
            }
        }
    }
}
