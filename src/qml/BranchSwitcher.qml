pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Igitt.Core
import IgittStyle as Styles

// The branches to switch to, from the tool bar: the local ones, then the
// remote-tracking ones, which make a local branch; filtered by what is
// typed, Enter taking the first. And a new branch at HEAD.
Popup {
    id: switcher

    required property Repository repo

    signal newBranchRequested()

    // The branches matching the filter, local ones first
    readonly property var entries: {
        const filter = filterField.text.trim().toLowerCase()
        const matches = name => filter === "" || name.toLowerCase().includes(filter)
        return switcher.repo.branches.filter(matches).map(name => ({name: name, remote: false}))
            .concat(switcher.repo.remoteBranches.filter(matches).map(name => ({name: name, remote: true})))
    }

    function choose(name: string) {
        close()
        if (name !== repo.branch)
            repo.checkoutBranch(name)
    }

    objectName: "branchSwitcher"
    width: 360
    padding: 8
    onOpened: {
        filterField.text = ""
        filterField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: 6

        TextField {
            id: filterField
            objectName: "branchFilter"
            Layout.fillWidth: true
            placeholderText: qsTr("Switch to a branch…")
            onAccepted: {
                if (switcher.entries.length > 0)
                    switcher.choose(switcher.entries[0].name)
            }
            Keys.onDownPressed: branchList.forceActiveFocus()
        }
        Label {
            Layout.fillWidth: true
            visible: switcher.repo.planning
            text: qsTr("Branches stay as they are while planning.")
            wrapMode: Text.Wrap
            color: Styles.TokenInterface.semantics.text_muted
        }
        ListView {
            id: branchList
            objectName: "branchList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 320)
            clip: true
            model: switcher.entries
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: entry

                required property var modelData
                required property int index
                readonly property bool current: modelData.name === switcher.repo.branch

                width: ListView.view.width
                height: 28
                topPadding: 4
                bottomPadding: 4
                enabled: !switcher.repo.planning
                highlighted: ListView.isCurrentItem && branchList.activeFocus
                onClicked: switcher.choose(modelData.name)
                Keys.onReturnPressed: switcher.choose(modelData.name)

                contentItem: RowLayout {
                    spacing: 6

                    Icon {
                        name: entry.current ? "check" : "git-branch"
                        opacity: entry.current ? 1 : 0.6
                    }
                    Label {
                        Layout.fillWidth: true
                        text: entry.modelData.name
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        font.bold: entry.current
                        opacity: entry.modelData.remote ? 0.7 : 1
                    }
                    Tag {
                        visible: entry.modelData.remote
                        text: qsTr("remote")
                        tint: Styles.TokenInterface.semantics.text_muted
                    }
                }
            }
        }
        Label {
            visible: switcher.entries.length === 0
            text: qsTr("No branch matches.")
            color: Styles.TokenInterface.semantics.text_muted
        }
        MenuSeparator {
            Layout.fillWidth: true
        }
        IconButton {
            objectName: "newBranchButton"
            Layout.fillWidth: true
            iconName: "plus"
            text: qsTr("New Branch at HEAD…")
            enabled: !switcher.repo.planning && switcher.repo.headSha !== ""
            onClicked: {
                switcher.close()
                switcher.newBranchRequested()
            }
        }
    }
}
