pragma Singleton
import QtQuick

// Tabs: a muted rounded bar, the chosen tab raised like a field
QtObject {
    id: root

    readonly property Semantics s: TokenInterface.semantics

    readonly property Style bar: Style {
        background: root.s.background_muted
        radius.value: Primitives.radius.r4
        // As high as the fields with its 28 px tabs
        padding.value: Primitives.spacing.sp025
        spacing: 0
    }

    readonly property Style button: Style {
        background: TokenInterface.transparent
        border.width: 0
        text {
            color: root.s.text_inactive
            weight: Primitives.fontWeight.medium
        }
        icon {
            size: Primitives.size.s16
            color: root.s.text_inactive
        }
        radius.value: Primitives.radius.r4
        padding {
            horizontal: Primitives.spacing.sp150
            vertical: Primitives.spacing.sp050
        }
        spacing: Primitives.spacing.sp100
        height: Primitives.size.s28
        // As wide as their text needs
        width: 0
        opacity: 1

        Style {
            state: StyleState.Disable
            opacity: 0.5
        }
    }

    // Tabs of a page: regular weight, a little less rounded
    readonly property Style segment: Style {
        text.weight: Primitives.fontWeight.regular
        radius.value: Primitives.radius.r3
    }

    // What a tab adds itself: chosen, then keyboard focus
    readonly property Style checked: Style {
        background: root.s.field_default
        border {
            width: Primitives.stroke.thin
            color: root.s.stroke_tab
        }
        text.color: root.s.text_default
        icon.color: root.s.text_default
    }
    readonly property Style focus: Style {
        border {
            width: Primitives.stroke.thin
            color: root.s.stroke_focus
        }
    }
}
