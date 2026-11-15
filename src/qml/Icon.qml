import QtQuick
import QtQuick.Controls.impl
import Igitt
import IgittStyle as Styles

// A Lucide icon on its own, tinted to `color`. An image only: it never
// takes clicks or hover, so put it in an item that should.
Item {
    id: icon

    required property string name
    property color color: Styles.TokenInterface.semantics.text_default
    property int size: 16

    implicitWidth: size
    implicitHeight: size

    IconImage {
        anchors.centerIn: parent
        width: icon.size
        height: icon.size
        source: Icons.source(icon.name)
        sourceSize.width: icon.size
        sourceSize.height: icon.size
        color: icon.color
        fillMode: Image.PreserveAspectFit
    }
}
