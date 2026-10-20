pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core

// The commit message as title, body and trailers, put together by
// MessageFormat with the limits from AppSettings.commit.
ColumnLayout {
    id: box

    required property Repository repo
    required property font monoFont

    readonly property var limits: AppSettings.commit
    readonly property int titleLength: title.text.trim().length
    readonly property bool titleTooLong: titleLength > limits.titleLimit
    readonly property int overlongLines: MessageFormat.countOverlongLines(body.text, limits.bodyWidth)
    readonly property bool canCommit: title.text.trim() !== "" && !repo.writing
                                      && (repo.staged.count > 0 || amend.checked)
                                      && !(limits.enforceTitleLimit && titleTooLong)

    function message(): string {
        const trailerList = []
        for (let i = 0; i < trailers.count; ++i)
            trailerList.push({key: trailers.get(i).key, value: trailers.get(i).value})
        return MessageFormat.compose(title.text, body.text, trailerList, limits.wrapBody ? limits.bodyWidth : 0)
    }

    function load(text: string) {
        const parts = MessageFormat.parse(text)
        title.text = parts.title
        body.text = parts.body
        trailers.clear()
        for (const trailer of parts.trailers)
            trailers.append(trailer)
    }

    function clear() {
        title.clear()
        body.clear()
        trailers.clear()
    }

    function addTrailer(key: string) {
        trailers.append({key: key, value: ""})
        Qt.callLater(() => trailerRows.itemAt(trailers.count - 1)?.focusValue())
    }

    function commit() {
        if (canCommit)
            repo.commit(message(), amend.checked)
    }

    spacing: 4

    ListModel {
        id: trailers
    }

    FontMetrics {
        id: metrics
        font: box.monoFont
    }

    Connections {
        target: box.repo
        function onCommitted() {
            box.clear()
            amend.checked = false
        }
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: title.activeFocus || body.activeFocus
        onActivated: box.commit()
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 4
        Layout.bottomMargin: 0

        TextField {
            id: title
            objectName: "commitTitle"
            Layout.fillWidth: true
            placeholderText: qsTr("Title: what this commit does")
            selectByMouse: true
            font.bold: true
            Keys.onReturnPressed: event => {
                if (event.modifiers & Qt.ControlModifier)
                    event.accepted = false
                else
                    body.forceActiveFocus()
            }
        }
        Label {
            text: `${box.titleLength}/${box.limits.titleLimit}`
            color: box.titleTooLong ? "#d62728" : box.palette.placeholderText
            font.family: box.monoFont.family
            ToolTip.visible: counterHover.hovered
            ToolTip.text: qsTr("The title should be at most %n characters.", "", box.limits.titleLimit)
            HoverHandler {
                id: counterHover
            }
        }
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 50
        Layout.leftMargin: 4
        Layout.rightMargin: 4

        TextArea {
            id: body
            objectName: "commitBody"
            placeholderText: qsTr("Body: why the change is needed")
            font: box.monoFont
            wrapMode: TextEdit.NoWrap
            selectByMouse: true

            // Where the body wraps.
            Rectangle {
                x: body.leftPadding + box.limits.bodyWidth * metrics.averageCharacterWidth
                width: 1
                height: Math.max(body.height, body.contentHeight)
                color: box.overlongLines > 0 ? "#d62728" : body.palette.mid
                opacity: 0.6
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        visible: box.overlongLines > 0

        Label {
            Layout.fillWidth: true
            text: box.limits.wrapBody
                  ? qsTr("%n line(s) longer than %1 characters; they are wrapped on commit.", "",
                         box.overlongLines).arg(box.limits.bodyWidth)
                  : qsTr("%n line(s) longer than %1 characters.", "", box.overlongLines).arg(box.limits.bodyWidth)
            wrapMode: Text.Wrap
            opacity: 0.7
            font.pixelSize: 11
        }
        ToolButton {
            text: qsTr("Wrap")
            onClicked: body.text = MessageFormat.wrapText(body.text, box.limits.bodyWidth)
        }
    }

    // Trailers: one contiguous block at the end of the message.
    Repeater {
        id: trailerRows

        model: trailers
        delegate: RowLayout {
            id: trailerRow

            required property int index
            required property string key
            required property string value

            function focusValue() {
                valueField.forceActiveFocus()
            }

            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4

            ComboBox {
                Layout.preferredWidth: 150
                editable: true
                model: box.limits.trailerKeys
                editText: trailerRow.key
                onEditTextChanged: trailers.setProperty(trailerRow.index, "key", editText)
            }
            TextField {
                id: valueField
                objectName: "trailerValue"
                Layout.fillWidth: true
                text: trailerRow.value
                placeholderText: qsTr("Value")
                selectByMouse: true
                onTextChanged: trailers.setProperty(trailerRow.index, "value", text)
            }
            ToolButton {
                text: "✕"
                onClicked: trailers.remove(trailerRow.index)
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
            text: qsTr("Amend")
            enabled: box.repo.headSha.length > 0
            onToggled: {
                if (checked && title.text.trim() === "" && body.text.trim() === "" && trailers.count === 0)
                    box.load(box.repo.headMessage)
            }
        }
        ToolButton {
            objectName: "addTrailerButton"
            text: qsTr("+ Trailer")
            onClicked: trailerMenu.popup(this, 0, height)

            Menu {
                id: trailerMenu

                Instantiator {
                    model: box.limits.trailerKeys
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData
                        onTriggered: box.addTrailer(modelData)
                    }
                    onObjectAdded: (index, object) => trailerMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => trailerMenu.removeItem(object)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Other…")
                    onTriggered: box.addTrailer("")
                }
            }
        }
        Item {
            Layout.fillWidth: true
        }
        Button {
            objectName: "commitButton"
            text: amend.checked ? qsTr("Amend") : qsTr("Commit")
            highlighted: true
            enabled: box.canCommit
            onClicked: box.commit()
        }
    }
}
