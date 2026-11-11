pragma Singleton
import QtQuick

// The color roles of the theme in use (Themes picks it, see Semantics);
// the control styles and igitt's own items only use `semantics`.
QtObject {
    readonly property Semantics semantics: Semantics {}
    // Whether the theme shown is a dark one
    readonly property bool dark: Themes.dark
    readonly property color transparent: "#00000000"

    // Text that reads on a fill of any color (a lane's, a status's): the
    // theme's darker or lighter one of text and surface
    function textOn(fill: color): color {
        const text = semantics.text_default
        const surface = semantics.background_elevated
        const textIsDark = text.hslLightness < surface.hslLightness
        const light = fill.hslLightness > 0.62
        return light === textIsDark ? text : surface
    }
}
