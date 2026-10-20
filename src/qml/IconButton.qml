import QtQuick
import QtQuick.Controls
import Igitt

// A tool button with a Lucide icon, tinted like its text.
ToolButton {
    property string iconName

    icon.source: iconName !== "" ? Icons.source(iconName) : ""
    icon.color: palette.buttonText
    icon.width: 16
    icon.height: 16
    display: text === "" ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
}
