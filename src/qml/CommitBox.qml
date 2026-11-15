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

    // `skipHooks`: once, without the pre-commit and commit-msg hooks
    function commit(skipHooks: bool) {
        if (canCommit)
            repo.commit(message(), amend.checked, skipHooks === true, amend.checked && resetAuthor.checked)
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
            resetAuthor.checked = false
            box.loadTemplate()
        }
        // A message, and above all an amend, is meant for the repository
        // it was written in, never the next one opened.
        function onPathChanged() {
            box.clear()
            amend.checked = false
            resetAuthor.checked = false
        }
        function onCommitTemplateChanged() {
            box.loadTemplate()
        }
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: box.editing
        onActivated: box.commit(false)
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
        }
    ]
    trailingItems: [
        Button {
            styles: [Styles.ButtonStyle.medium]
            objectName: "commitButton"
            text: amend.checked ? qsTr("Amend") : qsTr("Commit")
            highlighted: true
            enabled: box.canCommit
            onClicked: box.commit(false)
        },
        IconButton {
            id: commitOptions
            objectName: "commitOptionsButton"
            styles: [Styles.ButtonStyle.medium]
            iconName: "chevron-down"
            enabled: box.canCommit
            // Upwards: the form is at the bottom of the window
            onClicked: commitMenu.popup(commitOptions, commitOptions.width - commitMenu.implicitWidth,
                                        -commitMenu.implicitHeight)
            ToolTip.visible: hovered && !commitMenu.opened
            ToolTip.delay: 500
            ToolTip.text: qsTr("Commit without the hooks")

            Menu {
                id: commitMenu

                MenuItem {
                    objectName: "commitWithoutHooksItem"
                    text: amend.checked ? qsTr("Amend Without the Hooks (--no-verify)")
                                        : qsTr("Commit Without the Hooks (--no-verify)")
                    onTriggered: box.commit(true)
                }
            }
        }
    ]
}
