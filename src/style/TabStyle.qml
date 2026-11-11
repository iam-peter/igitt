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
        includes: [root.large]
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
        spacing: Primitives.spacing.sp100
        // As wide as their text needs
        width: 0
        opacity: 1

        Style {
            state: StyleState.Disable
            opacity: 0.5
        }
    }

    // Sizes of a tab, by its height; the bar is 4 px higher. Large is the
    // default, as high as the large fields with its bar.
    readonly property Style large: Style {
        height: Primitives.size.s28
        padding {
            horizontal: Primitives.spacing.sp150
            vertical: Primitives.spacing.sp050
        }
    }
    readonly property Style medium: Style {
        height: Primitives.size.s24
        padding {
            horizontal: Primitives.spacing.sp125
            vertical: Primitives.spacing.sp025
        }
    }
    readonly property Style small: Style {
        height: Primitives.size.s20
        padding {
            horizontal: Primitives.spacing.sp100
            vertical: 0
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
