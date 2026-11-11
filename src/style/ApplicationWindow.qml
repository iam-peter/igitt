import QtQuick
import QtQuick.Templates as T

// A window in the theme's colors. Its palette is the theme's too, for
// the controls this style leaves to Basic (SplitView, BusyIndicator) and
// for items that read the palette.
T.ApplicationWindow {
    readonly property Semantics s: TokenInterface.semantics

    // Dialogs, popups and menus shown over the window (PopupHold). While
    // there are any, the window's drag handlers stay off: Qt lets them take
    // a press that something in a popup above them took.
    property int popupsOpen: 0
    readonly property bool popupOpen: popupsOpen > 0

    color: s.background_default

    palette.window: s.background_default
    palette.windowText: s.text_default
    palette.base: s.background_elevated
    palette.alternateBase: s.background_stripe
    palette.text: s.text_default
    palette.button: s.background_secondary
    palette.buttonText: s.text_default
    palette.brightText: s.text_on_danger
    palette.highlight: s.primary_default
    palette.highlightedText: s.text_on_primary
    palette.placeholderText: s.text_muted
    // What igitt draws in tooltip colors (a dragged row's label, a
    // comment) looks like a popover
    palette.toolTipBase: s.background_elevated
    palette.toolTipText: s.text_default
    palette.link: s.text_link
    palette.linkVisited: s.text_link
    palette.light: s.background_elevated
    palette.midlight: s.stroke_subtle
    palette.mid: s.stroke_subtle
    palette.dark: s.stroke_focus
    palette.shadow: s.dim
}
