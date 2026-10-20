pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Igitt.Core

Pane {
    id: view

    required property Repository repo

    padding: 0

    ListView {
        id: list

        function showCurrent() {
            if (currentIndex >= 0)
                view.repo.showCommit(view.repo.commits.shaAt(currentIndex))
        }

        anchors.fill: parent
        clip: true
        focus: true
        model: view.repo.commits
        currentIndex: -1
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        keyNavigationEnabled: false
        ScrollBar.vertical: ScrollBar {}

        // Selection follows the user, not model resets, so a refresh never
        // replaces the diff on display.
        Keys.onUpPressed: {
            decrementCurrentIndex()
            showCurrent()
        }
        Keys.onDownPressed: {
            incrementCurrentIndex()
            showCurrent()
        }

        Connections {
            target: view.repo.commits
            function onModelReset() {
                list.currentIndex = view.repo.commits.rowOf(view.repo.diffCommit)
            }
        }

        delegate: CommitDelegate {
            width: ListView.view.width
            onClicked: {
                list.currentIndex = index
                list.forceActiveFocus()
                list.showCurrent()
            }
        }
    }
}
