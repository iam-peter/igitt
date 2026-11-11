pragma Singleton
import QtQuick

// How a control's state for its styles is read, in one place. Values
// such as checked (a check box), highlighted (a list row) or keyboard
// focus are no states: the controls add them as overrides.
QtObject {
    // Buttons: pressed or checked is Active
    function pressable(control): int {
        return !control.enabled ? StyleState.Disable
             : control.down || control.checked ? StyleState.Active
             : control.hovered ? StyleState.Hover : StyleState.Idle
    }

    // Entries of lists, menus and tabs: only pressed is Active; a checked
    // menu entry or the current tab is not pressed
    function item(control): int {
        return !control.enabled ? StyleState.Disable
             : control.down ? StyleState.Active
             : control.hovered ? StyleState.Hover : StyleState.Idle
    }

    // Fields and toggles: under the pointer or not
    function hoverable(control): int {
        return !control.enabled ? StyleState.Disable
             : control.hovered ? StyleState.Hover : StyleState.Idle
    }
}
