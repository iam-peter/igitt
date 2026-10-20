import QtQuick
import QtQuick.Controls

// A branch, tag or stash name, in the color of its commit's lane. Local
// branches are filled; remote-only branches, tags and stashes outlined.
Rectangle {
    id: label

    required property string name
    required property string kind // head, branch, remote, tag, stash, or more: "+N" others
    required property color laneColor
    required property bool current
    // Remote-tracking branches at the same commit; for "more", the others.
    property var remotes: []

    readonly property bool filled: kind === "branch" || kind === "head"
    readonly property color textColor: filled ? (laneColor.hslLightness > 0.62 ? "#202020" : "white")
                                              : laneColor

    implicitWidth: text.implicitWidth + 12
    implicitHeight: 18
    radius: kind === "tag" ? 2 : 9
    color: filled ? laneColor : Qt.alpha(laneColor, 0.12)
    border.width: filled ? (current ? 2 : 0) : 1
    border.color: filled ? Qt.darker(laneColor, 1.5) : laneColor

    ToolTip.visible: hover.hovered
    ToolTip.delay: 500
    ToolTip.text: {
        if (kind === "more")
            return remotes.join("\n")
        const lines = [`${kind === "remote" ? qsTr("remote branch") : kind}: ${name}`]
        if (current)
            lines.push(qsTr("checked out"))
        for (const remote of remotes)
            lines.push(qsTr("also %1").arg(remote))
        return lines.join("\n")
    }

    HoverHandler {
        id: hover
    }

    Label {
        id: text
        anchors.centerIn: parent
        text: {
            const prefix = label.current ? "✓ " : label.kind === "tag" ? "◆ " : ""
            const suffix = label.kind !== "more" && label.remotes.length > 0 ? "  ☁" : ""
            return prefix + label.name + suffix
        }
        textFormat: Text.PlainText
        color: label.textColor
        font.pixelSize: 11
        font.bold: label.current
    }
}
