pragma Singleton
import QtQuick

// Entries of lists, menus and combo box lists: a row that lights up in
// the accent gray. Highlighted (the chosen row, the menu entry under the
// keyboard or pointer) is an override the item adds itself.
QtObject {
    id: root

    readonly property Semantics s: TokenInterface.semantics

    readonly property Style base: Style {
        background: TokenInterface.transparent
        text.color: root.s.text_default
        icon {
            color: root.s.text_default
            size: Primitives.size.s16
        }
        radius.value: Primitives.radius.r2
        padding.value: Primitives.spacing.sp100
        spacing: Primitives.spacing.sp100
        height: Primitives.size.s32
        opacity: 1

        Style {
            state: StyleState.Disable
            opacity: 0.5
        }
    }

    // ItemDelegate: also lights up under the pointer
    readonly property Style delegate: Style {
        width: Primitives.size.s100

        Style {
            state: StyleState.Hover
            background: root.s.background_accent
        }
        Style {
            state: StyleState.Active
            background: root.s.background_accent
        }
    }

    // Menu entries: as wide as their text needs, at least 120 px; the
    // icon muted, the tick and the submenu arrow in the text color
    readonly property Style menuItem: Style {
        width: Primitives.size.s120
        icon.color: root.s.text_muted

        Style {
            state: StyleState.Active
            background: root.s.background_accent
        }
    }

    // The rows of a combo box's list, with room for the tick on the right
    readonly property Style comboItem: Style {
        padding.right: Primitives.spacing.sp100 + Primitives.size.s16 + Primitives.spacing.sp100
    }

    // A row of igitt's lists: rounded a little less, and under the
    // pointer or pressed tinted gray instead of the accent, so the chosen
    // row stays the one that stands out
    readonly property Style listRow: Style {
        radius.value: Primitives.radius.r3
        height: Primitives.size.s28
        width: 0

        Style {
            state: StyleState.Hover
            background: Qt.tint(root.s.background_elevated, Qt.rgba(0.5, 0.5, 0.5, 0.12))
        }
        Style {
            state: StyleState.Active
            background: Qt.tint(root.s.background_elevated, Qt.rgba(0.5, 0.5, 0.5, 0.25))
        }
    }

    readonly property Style highlighted: Style {
        background: root.s.background_accent
    }
}
