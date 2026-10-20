pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core

// The message of the next commit, and the buttons to make it.
MessageEditor {
    id: box

    required property Repository repo

    readonly property bool canCommit: titleAccepted && !repo.writing && (repo.staged.count > 0 || amend.checked)

    function commit() {
        if (canCommit)
            repo.commit(message(), amend.checked)
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
        enabled: box.editing
        onActivated: box.commit()
    }

    leadingItems: [
        CheckBox {
            id: amend
            text: qsTr("Amend")
            enabled: box.repo.headSha.length > 0
            onToggled: {
                if (checked && box.empty)
                    box.load(box.repo.headMessage)
            }
        }
    ]
    trailingItems: [
        Button {
            objectName: "commitButton"
            text: amend.checked ? qsTr("Amend") : qsTr("Commit")
            highlighted: true
            enabled: box.canCommit
            onClicked: box.commit()
        }
    ]
}
