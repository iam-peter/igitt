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
        anchors.fill: parent
        orientation: Qt.Vertical

        // Drag a file onto the other list to stage or unstage it.
        FileList {
            id: unstagedFiles
            SplitView.fillHeight: true
            SplitView.minimumHeight: 80
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
            SplitView.preferredHeight: 160
            SplitView.minimumHeight: 80
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
            objectName: "commitBox"
            SplitView.preferredHeight: 230
            SplitView.minimumHeight: 150
            repo: pane.repo
            monoFont: pane.monoFont
        }
    }
}
