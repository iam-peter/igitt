pragma Singleton
import QtQuick

// Controls in a row that belong together (a field and its buttons) share
// their inner edges: those corners are square. A control puts the style
// for its position last in its overrides.
QtObject {
    id: root

    enum Position {
        None,
        First,
        Middle,
        Last
    }

    readonly property Style first: Style {
        radius {
            topRight: 0
            bottomRight: 0
        }
    }
    readonly property Style middle: Style {
        radius.value: 0
    }
    readonly property Style last: Style {
        radius {
            topLeft: 0
            bottomLeft: 0
        }
    }

    function of(position: int): Style {
        switch (position) {
        case GroupStyle.Position.First:
            return root.first
        case GroupStyle.Position.Middle:
            return root.middle
        case GroupStyle.Position.Last:
            return root.last
        }
        return null
    }
}
