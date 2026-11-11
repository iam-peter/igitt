import QtQuick
import QtQuick.Controls
import Igitt
import IgittStyle as Styles

// A tool button with a Lucide icon, tinted like its text. Small, as most
// sit in the bars above lists and diffs; the main tool bar's are medium.
ToolButton {
    property string iconName

    styles: [Styles.ButtonStyle.small]
    icon.source: iconName !== "" ? Icons.source(iconName) : ""
    icon.color: palette.buttonText
    icon.width: 16
    icon.height: 16
    display: text === "" ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
}
