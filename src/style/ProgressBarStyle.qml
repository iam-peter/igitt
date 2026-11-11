pragma Singleton
import QtQuick

// A rounded track in the accent's tint, the progress in the accent
QtObject {
    id: root

    readonly property Semantics s: TokenInterface.semantics

    readonly property Style base: Style {
        width: Primitives.size.s200
        height: Primitives.size.s8
        radius.value: Primitives.radius.r4

        Style {
            part: StylePart.Track
            background: Primitives.alpha(root.s.primary_default, 0.2)
        }
        Style {
            part: StylePart.Progress
            background: root.s.primary_default
        }
    }
}
