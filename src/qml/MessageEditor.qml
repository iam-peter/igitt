pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core

// A commit message as title, body and trailers, put together by
// MessageFormat with the limits from AppSettings.commit.
ColumnLayout {
    id: box

    required property font monoFont
    // The fields' object names are <namePrefix>Title and <namePrefix>Body.
    property string namePrefix: "commit"
    // Items at the start and at the end of the bottom row.
    property alias leadingItems: leadingRow.data
    property alias trailingItems: trailingRow.data

    readonly property var limits: AppSettings.commit
    readonly property bool hasTitle: title.text.trim() !== ""
    readonly property int titleLength: title.text.trim().length
    readonly property bool titleTooLong: titleLength > limits.titleLimit
    // A message with this title may be used.
    readonly property bool titleAccepted: hasTitle && !(limits.enforceTitleLimit && titleTooLong)
    readonly property bool empty: !hasTitle && body.text.trim() === "" && trailers.count === 0
    readonly property bool editing: title.activeFocus || body.activeFocus
    readonly property int overlongLines: MessageFormat.countOverlongLines(body.text, limits.bodyWidth)

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

    function focusTitle() {
        title.forceActiveFocus()
    }

    spacing: 4

    ListModel {
        id: trailers
    }

    FontMetrics {
        id: metrics
        font: box.monoFont
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 4
        Layout.bottomMargin: 0

        TextField {
            id: title
            objectName: box.namePrefix + "Title"
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

    BoundedScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 50
        Layout.leftMargin: 4
        Layout.rightMargin: 4

        TextArea {
            id: body
            objectName: box.namePrefix + "Body"
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
                objectName: "trailerKey"
                Layout.preferredWidth: 150
                editable: true
                model: box.limits.trailerKeys
                // Set once rather than bound: loading the model selects its
                // first entry, which would overwrite the key that was picked.
                Component.onCompleted: {
                    currentIndex = find(trailerRow.key)
                    editText = trailerRow.key
                }
                // Only what the user picks or types changes the trailer.
                onActivated: index => trailers.setProperty(trailerRow.index, "key", textAt(index))
                onEditTextChanged: {
                    if (activeFocus)
                        trailers.setProperty(trailerRow.index, "key", editText)
                }
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
            IconButton {
                iconName: "x"
                onClicked: trailers.remove(trailerRow.index)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        Layout.bottomMargin: 4

        RowLayout {
            id: leadingRow
            Layout.fillWidth: false
        }
        IconButton {
            iconName: "plus"
            objectName: box.namePrefix + "AddTrailerButton"
            text: qsTr("Trailer")
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
        RowLayout {
            id: trailingRow
            Layout.fillWidth: false
        }
    }
}
