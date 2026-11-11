import QtQuick
import QtQuick.Templates as T

// Counts a popup on its window while it is shown (ApplicationWindow's
// popupsOpen), so the window can keep its drag handlers off meanwhile.
// Qt hands a press that something inside a popup takes to the pointer
// handlers of what lies under the popup as well; a DragHandler there then
// drags whatever it is on, under a modal dialog too.
QtObject {
    id: hold

    required property T.Popup popup
    // The window counted on, while the popup is shown
    property var window: null

    function take() {
        release()
        const w = popup.parent ? popup.parent.Window.window : null
        if (w && w.popupsOpen !== undefined) {
            window = w
            ++w.popupsOpen
        }
    }
    function release() {
        if (window) {
            --window.popupsOpen
            window = null
        }
    }

    property Connections connections: Connections {
        target: hold.popup
        function onAboutToShow() {
            hold.take()
        }
        function onClosed() {
            hold.release()
        }
    }
    Component.onDestruction: release()
}
