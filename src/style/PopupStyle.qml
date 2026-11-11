pragma Singleton
import QtQuick

// Everything that floats over the window: popovers, menus, the list of
// a combo box, dialogs and tooltips. dim is what lies over the window
// behind a modal one.
QtObject {
    id: root

    readonly property Semantics s: TokenInterface.semantics

    readonly property Style popover: Style {
        background: root.s.background_elevated
        border {
            width: Primitives.stroke.thin
            color: root.s.stroke_subtle
        }
        radius.value: Primitives.radius.r4
        padding.value: Primitives.spacing.sp150
        dim: root.s.dim
    }

    readonly property Style menu: Style {
        includes: [root.popover]
        padding.value: Primitives.spacing.sp050
        width: Primitives.size.s128
        height: Primitives.size.s40
    }

    // A combo box's list, a little below the box
    readonly property Style list: Style {
        includes: [root.popover]
        padding.value: Primitives.spacing.sp050
        spacing: Primitives.spacing.sp050
    }

    // A card on the window's color, the title in semibold
    readonly property Style dialog: Style {
        includes: [root.popover]
        background: root.s.background_default
        padding {
            horizontal: Primitives.spacing.sp200
            top: Primitives.spacing.sp100
            bottom: Primitives.spacing.sp200
        }

        Style {
            part: StylePart.Label
            text {
                color: root.s.text_default
                size: Primitives.fontSize.title
                weight: Primitives.fontWeight.semiBold
            }
            padding {
                horizontal: Primitives.spacing.sp200
                top: Primitives.spacing.sp200
                bottom: Primitives.spacing.sp100
            }
        }
    }
    // A dialog that leaves the window usable darkens it only a little
    readonly property Style modeless: Style {
        dim: root.s.dim_subtle
    }

    // The accent color, small text
    readonly property Style toolTip: Style {
        background: root.s.primary_default
        text {
            color: root.s.text_on_primary
            size: Primitives.fontSize.small
        }
        radius.value: Primitives.radius.r4
        padding {
            horizontal: Primitives.spacing.sp100
            vertical: Primitives.spacing.sp050
        }
        spacing: Primitives.spacing.sp100
    }
}
