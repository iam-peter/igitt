pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core

Pane {
    id: pane

    required property Repository repo
    required property font monoFont

    signal timelapseRequested(string path)

    padding: 0

    SplitView {

        handle: SplitterHandle {}
        anchors.fill: parent
        orientation: Qt.Vertical

        // Drag a file onto the other list to stage or unstage it.
        FileList {
            id: unstagedFiles
            SplitView.fillHeight: true
            SplitView.minimumHeight: 56
            repo: pane.repo
            staged: false
            title: qsTr("Unstaged Changes")
            onTimelapseRequested: path => pane.timelapseRequested(path)
            onFileDragMoved: (path, pos) => stagedFiles.dropTarget = stagedFiles.containsScenePoint(pos)
            onFileDropped: (path, pos) => {
                stagedFiles.dropTarget = false
                if (stagedFiles.containsScenePoint(pos))
                    pane.repo.stageFile(path)
            }
        }

        FileList {
            id: stagedFiles
            SplitView.preferredHeight: 110
            SplitView.minimumHeight: 56
            repo: pane.repo
            staged: true
            title: qsTr("Staged Changes")
            onTimelapseRequested: path => pane.timelapseRequested(path)
            onFileDragMoved: (path, pos) => unstagedFiles.dropTarget = unstagedFiles.containsScenePoint(pos)
            onFileDropped: (path, pos) => {
                unstagedFiles.dropTarget = false
                if (unstagedFiles.containsScenePoint(pos))
                    pane.repo.unstageFile(path)
            }
        }

        CommitBox {
            id: commitBox
            objectName: "commitBox"
            // Trailers make it grow, so the buttons below them stay in view.
            SplitView.preferredHeight: Math.max(210, commitBox.implicitHeight)
            SplitView.minimumHeight: Math.max(120, commitBox.implicitHeight)
            repo: pane.repo
            monoFont: pane.monoFont
        }
    }
}
