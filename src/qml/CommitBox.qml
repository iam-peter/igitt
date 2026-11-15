pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt
import Igitt.Core
import IgittStyle as Styles

// The message of the next commit, and the buttons to make it.
MessageEditor {
    id: box

    required property Repository repo

    // The template as it was loaded, not written over yet
    readonly property bool untouchedTemplate: repo.commitTemplate.trim() !== ""
                                              && message().trim() === repo.commitTemplate.trim()
    readonly property bool canCommit: titleAccepted && !untouchedTemplate && !repo.writing
                                      && (repo.staged.count > 0 || amend.checked)

    function commit() {
        if (canCommit)
            repo.commit(message(), amend.checked, skipHooks.checked, amend.checked && resetAuthor.checked)
    }
    // commit.template, to fill in, while nothing else is written
    function loadTemplate() {
        if (empty && !amend.checked && repo.commitTemplate !== "")
            load(repo.commitTemplate)
    }

    Component.onCompleted: loadTemplate()

    Connections {
        target: box.repo
        function onCommitted() {
            box.clear()
            amend.checked = false
            skipHooks.checked = false
            resetAuthor.checked = false
            box.loadTemplate()
        }
        // A message, and above all an amend, is meant for the repository
        // it was written in, never the next one opened.
        function onPathChanged() {
            box.clear()
            amend.checked = false
            skipHooks.checked = false
            resetAuthor.checked = false
        }
        function onCommitTemplateChanged() {
            box.loadTemplate()
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
            styles: [Styles.ToggleStyle.small]
            objectName: "amendBox"
            text: qsTr("Amend")
            enabled: box.repo.headSha.length > 0
            onToggled: {
                if (checked && (box.empty || box.untouchedTemplate))
                    box.load(box.repo.headMessage)
                else if (!checked && box.message().trim() === box.repo.headMessage.trim()) {
                    box.clear()
                    box.loadTemplate()
                }
            }
        },
        CheckBox {
            id: resetAuthor
            styles: [Styles.ToggleStyle.small]
            objectName: "resetAuthorBox"
            visible: amend.checked
            text: qsTr("As Me")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Make the amended commit yours, dated now (--reset-author)")
        },
        CheckBox {
            id: skipHooks
            styles: [Styles.ToggleStyle.small]
            objectName: "skipHooksBox"
            text: qsTr("Skip Hooks")
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Commit without running the pre-commit and commit-msg hooks (--no-verify), once")
        }
    ]
    trailingItems: [
        Button {
            styles: [Styles.ButtonStyle.medium]
            objectName: "commitButton"
            text: amend.checked ? qsTr("Amend") : qsTr("Commit")
            highlighted: true
            enabled: box.canCommit
            onClicked: box.commit()
        }
    ]
}
