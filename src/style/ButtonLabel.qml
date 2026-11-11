import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

// The icon and text of a button, in its style's colors
IconLabel {
    required property T.AbstractButton control
    required property StyleCascade cascade

    spacing: control.spacing
    mirrored: control.mirrored
    display: control.display

    icon.source: control.icon.source
    icon.name: control.icon.name
    icon.width: control.icon.width
    icon.height: control.icon.height
    icon.color: cascade.icon.color
    text: control.text
    font: control.font
    color: cascade.text.color
}
