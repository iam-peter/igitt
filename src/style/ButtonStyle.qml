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
        includes: [root.large]
        radius.value: Primitives.radius.r4
        text.weight: Primitives.fontWeight.medium
        icon.size: Primitives.size.s16
        opacity: 1

        Style {
            state: StyleState.Disable
            opacity: 0.5
        }
    }

    // Sizes, by the height of the button. Large is the default; text and
    // icons fit in it like in the fields: 8 px above and below made a
    // button with text 33 px high
    readonly property Style large: Style {
        height: Primitives.size.s32
        width: Primitives.size.s64
        radius.value: Primitives.radius.r4
        padding {
            horizontal: Primitives.spacing.sp150
            vertical: Primitives.spacing.sp075
        }
        spacing: Primitives.spacing.sp100
    }
    readonly property Style medium: Style {
        height: Primitives.size.s28
        width: Primitives.size.s56
        radius.value: Primitives.radius.r4
        padding {
            horizontal: Primitives.spacing.sp125
            vertical: Primitives.spacing.sp050
        }
        spacing: Primitives.spacing.sp075
    }
    readonly property Style small: Style {
        height: Primitives.size.s24
        width: Primitives.size.s48
        radius.value: Primitives.radius.r3
        padding {
            horizontal: Primitives.spacing.sp100
            vertical: Primitives.spacing.sp025
        }
        spacing: Primitives.spacing.sp050
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

    // Tool buttons: ghost; without text they are square, as any button
    readonly property Style tool: Style {
        includes: [root.ghost]
    }

    // What a button forces itself, over any variant
    readonly property Style focus: Style {
        // Focus shows in the border, without a glow
        border {
            width: Primitives.stroke.thin
            color: root.s.stroke_focus
        }
    }
    // Without text a button is as wide as it is high (Button.qml)
    readonly property Style iconOnly: Style {
        padding.horizontal: Primitives.spacing.sp050
    }

    // The variant Qt's flags ask for
    function variant(highlighted: bool, flat: bool): Style {
        return highlighted ? root.primary : flat ? root.ghost : root.outline
    }
}
