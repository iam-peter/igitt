pragma Singleton
import QtQuick

// Buttons and tool buttons. base holds what every button shares, a
// variant its colors; a button picks one with `styles` (Qt's highlighted
// and flat pick primary and ghost). Every variant sets the same values in
// the same states, so one can replace another.
QtObject {
    id: root

    readonly property Semantics s: TokenInterface.semantics

    readonly property Style base: Style {
        height: Primitives.size.s32
        width: Primitives.size.s64
        radius.value: Primitives.radius.r4
        // Text and icons fit in one control height (32 px) like the
        // fields: 8 px above and below made a button with text 33 px high
        padding {
            horizontal: Primitives.spacing.sp150
            vertical: Primitives.spacing.sp075
        }
        spacing: Primitives.spacing.sp100
        text.weight: Primitives.fontWeight.medium
        icon.size: Primitives.size.s16
        opacity: 1

        Style {
            state: StyleState.Disable
            opacity: 0.5
        }
    }

    // Filled with the accent: the one action a dialog or form is for
    readonly property Style primary: Style {
        background: root.s.primary_default
        border.width: 0
        text.color: root.s.text_on_primary
        icon.color: root.s.text_on_primary

        Style {
            state: StyleState.Hover
            background: Primitives.alpha(root.s.primary_default, 0.9)
        }
        Style {
            state: StyleState.Active
            background: Primitives.alpha(root.s.primary_default, 0.9)
        }
    }

    // The default: a light surface with a border
    readonly property Style outline: Style {
        background: root.s.outline_default
        border {
            width: Primitives.stroke.thin
            color: root.s.stroke_outline
        }
        text.color: root.s.text_default
        icon.color: root.s.text_default

        Style {
            state: StyleState.Hover
            background: root.s.outline_hover
        }
        Style {
            state: StyleState.Active
            background: root.s.outline_hover
        }
    }

    // No surface until the pointer is over it: tool bars, pickers
    readonly property Style ghost: Style {
        background: TokenInterface.transparent
        border.width: 0
        text.color: root.s.text_default
        icon.color: root.s.text_default

        Style {
            state: StyleState.Hover
            background: root.s.background_hover
        }
        Style {
            state: StyleState.Active
            background: root.s.background_hover
        }
    }

    // Deleting
    readonly property Style destructive: Style {
        background: root.s.danger_fill
        border.width: 0
        text.color: root.s.text_on_danger
        icon.color: root.s.text_on_danger

        Style {
            state: StyleState.Hover
            background: Primitives.alpha(root.s.danger_fill, 0.9 * root.s.danger_fill.a)
        }
        Style {
            state: StyleState.Active
            background: Primitives.alpha(root.s.danger_fill, 0.9 * root.s.danger_fill.a)
        }
    }

    // A ghost button inside a row: its hover is a tint of the text
    // color, so it also shows on a chosen row
    readonly property Style inline: Style {
        background: TokenInterface.transparent
        border.width: 0
        radius.value: Primitives.radius.r3
        text.color: root.s.text_default
        icon.color: root.s.text_default

        Style {
            state: StyleState.Hover
            background: Primitives.alpha(root.s.text_default, 0.14)
        }
        Style {
            state: StyleState.Active
            background: Primitives.alpha(root.s.text_default, 0.24)
        }
    }

    // Tool buttons: ghost, square without text
    readonly property Style tool: Style {
        includes: [root.ghost]
        width: Primitives.size.s32
    }

    // What a button forces itself, over any variant
    readonly property Style focus: Style {
        // Focus shows in the border, without a glow
        border {
            width: Primitives.stroke.thin
            color: root.s.stroke_focus
        }
    }
    readonly property Style iconOnly: Style {
        width: Primitives.size.s32
        padding.horizontal: Primitives.spacing.sp100
    }

    // The variant Qt's flags ask for
    function variant(highlighted: bool, flat: bool): Style {
        return highlighted ? root.primary : flat ? root.ghost : root.outline
    }
}
