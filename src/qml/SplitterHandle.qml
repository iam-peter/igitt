import QtQuick
import QtQuick.Controls

// A SplitView handle that is easy to grab: 7 px to hit, a thin line to
// see, highlighted while hovered or dragged.
Control {
    id: handle

    readonly property bool horizontal: width > height
    readonly property bool active: SplitHandle.hovered || SplitHandle.pressed

    implicitWidth: 7
    implicitHeight: 7

    background: Item {
        Rectangle {
            anchors.centerIn: parent
            width: handle.horizontal ? parent.width : (handle.active ? 3 : 1)
            height: handle.horizontal ? (handle.active ? 3 : 1) : parent.height
            color: handle.SplitHandle.pressed ? handle.palette.highlight
                 : handle.SplitHandle.hovered ? Qt.darker(handle.palette.mid, 1.3)
                 : handle.palette.mid
        }
    }
}
