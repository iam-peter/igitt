pragma Singleton
import QtQuick

// Check boxes, radio buttons and switches: a label beside an indicator.
// The indicator (box, circle, track) and the switch's thumb are parts;
// accent colors the mark inside the indicator: the tick, the dot.
//
// Checked is a value, not a passing state: a disabled box stays checked.
// So it is an override the control adds, after focus, while the states
// stay disabled, hover and idle.
QtObject {
    id: root

    readonly property Semantics s: TokenInterface.semantics

    readonly property Style base: Style {
        text.color: root.s.text_default
        padding.value: Primitives.spacing.sp050
        spacing: Primitives.spacing.sp100
        opacity: 1

        Style {
            state: StyleState.Disable
            opacity: 0.5
        }
        // An icon before the label (icon.source): spacing is its gap to
        // the text
        Style {
            part: StylePart.Icon
            icon {
                size: Primitives.size.s16
                color: root.s.text_default
            }
            spacing: Primitives.spacing.sp075
        }
    }

    readonly property Style checkBox: Style {
        Style {
            part: StylePart.Indicator
            width: Primitives.size.s16
            radius.value: Primitives.radius.r4
            background: root.s.field_default
            border {
                width: Primitives.stroke.thin
                color: root.s.stroke_field
            }
            accent: root.s.text_on_primary
        }
    }
    // Checked or partly checked: filled, the border too
    readonly property Style checkBoxChecked: Style {
        Style {
            part: StylePart.Indicator
            background: root.s.primary_default
            border.color: root.s.primary_default
        }
    }

    readonly property Style radioButton: Style {
        Style {
            part: StylePart.Indicator
            width: Primitives.size.s16
            radius.value: Primitives.size.s16 / 2
            background: root.s.field_default
            border {
                width: Primitives.stroke.thin
                color: root.s.stroke_field
            }
            accent: root.s.primary_default
        }
    }

    readonly property Style switchControl: Style {
        Style {
            part: StylePart.Indicator
            width: Primitives.size.s32
            height: Primitives.size.s18
            background: root.s.stroke_field
            border {
                width: 0
                color: root.s.stroke_focus
            }
        }
        Style {
            part: StylePart.Handle
            width: Primitives.size.s16
            background: root.s.switch_thumb
        }
    }
    readonly property Style switchChecked: Style {
        Style {
            part: StylePart.Indicator
            background: root.s.primary_default
        }
        Style {
            part: StylePart.Handle
            background: root.s.switch_thumb_checked
        }
    }

    // Keyboard focus shows in the indicator's border
    readonly property Style focus: Style {
        Style {
            part: StylePart.Indicator
            border {
                width: Primitives.stroke.thin
                color: root.s.stroke_focus
            }
        }
    }
}
