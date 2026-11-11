pragma Singleton
import QtQuick

// Scroll bars and indicators: only the thumb shows, as the Handle part
QtObject {
    id: root

    readonly property Semantics s: TokenInterface.semantics

    readonly property Style base: Style {
        padding.value: Primitives.spacing.sp025

        Style {
            part: StylePart.Handle
            background: root.s.scroll_thumb
            width: Primitives.size.s6
        }
    }

    // A bar that cannot be dragged, and the scroll indicator, are thin
    readonly property Style thin: Style {
        Style {
            part: StylePart.Handle
            width: Primitives.size.s2
        }
    }
}
