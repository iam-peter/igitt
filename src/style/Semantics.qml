import QtQuick

// The roles of the colors in the theme in use, the only colors the
// control styles and igitt's own items use. The theme files give every
// role a value (see docs/THEMES.md); Themes reads them, and this passes
// them on with their types.
QtObject {
    // Text and icons; inactive: tabs that are not chosen
    readonly property color text_default: Themes.roles.text_default
    readonly property color text_muted: Themes.roles.text_muted
    readonly property color text_inactive: Themes.roles.text_inactive
    readonly property color text_on_primary: Themes.roles.text_on_primary
    readonly property color text_on_danger: Themes.roles.text_on_danger
    readonly property color text_link: Themes.roles.text_link

    // The accent: filled buttons, text selections, the tooltip
    readonly property color primary_default: Themes.roles.primary_default

    // What things are: danger for errors and removed files (its fill for
    // buttons), success for added and approved, warning for changed and
    // outdated, info for renamed and open reviews, special for merged and
    // planned
    readonly property color danger_default: Themes.roles.danger_default
    readonly property color danger_fill: Themes.roles.danger_fill
    readonly property color success_default: Themes.roles.success_default
    readonly property color warning_default: Themes.roles.warning_default
    readonly property color info_default: Themes.roles.info_default
    readonly property color special_default: Themes.roles.special_default
    readonly property color icon_folder: Themes.roles.icon_folder

    // Surfaces: the window (default), lists, panels and popups
    // (elevated), secondary buttons, chosen rows and quiet highlights
    // (accent), flat buttons under the pointer (hover), every other row
    // of a list (stripe), the chosen lines of a diff (selection)
    readonly property color background_default: Themes.roles.background_default
    readonly property color background_elevated: Themes.roles.background_elevated
    readonly property color background_muted: Themes.roles.background_muted
    readonly property color background_secondary: Themes.roles.background_secondary
    readonly property color background_accent: Themes.roles.background_accent
    readonly property color background_hover: Themes.roles.background_hover
    readonly property color background_stripe: Themes.roles.background_stripe
    readonly property color selection_default: Themes.roles.selection_default

    // Fields: text fields, combo boxes, the chosen tab
    readonly property color field_default: Themes.roles.field_default
    readonly property color field_hover: Themes.roles.field_hover

    // Outlined buttons
    readonly property color outline_default: Themes.roles.outline_default
    readonly property color outline_hover: Themes.roles.outline_hover

    // Lines: panels and separators (subtle), the border of fields,
    // keyboard focus, outlined buttons, the chosen tab
    readonly property color stroke_subtle: Themes.roles.stroke_subtle
    readonly property color stroke_field: Themes.roles.stroke_field
    readonly property color stroke_focus: Themes.roles.stroke_focus
    readonly property color stroke_outline: Themes.roles.stroke_outline
    readonly property color stroke_tab: Themes.roles.stroke_tab

    readonly property color switch_thumb: Themes.roles.switch_thumb
    readonly property color switch_thumb_checked: Themes.roles.switch_thumb_checked
    readonly property color scroll_thumb: Themes.roles.scroll_thumb

    // Behind the lines of a diff, and behind the words that changed in them
    readonly property color diff_added: Themes.roles.diff_added
    readonly property color diff_removed: Themes.roles.diff_removed
    readonly property color diff_hunk: Themes.roles.diff_hunk
    readonly property color diff_added_word: Themes.roles.diff_added_word
    readonly property color diff_removed_word: Themes.roles.diff_removed_word

    // Code; heading is Markdown's
    readonly property color syntax_keyword: Themes.roles.syntax_keyword
    readonly property color syntax_type: Themes.roles.syntax_type
    readonly property color syntax_function: Themes.roles.syntax_function
    readonly property color syntax_string: Themes.roles.syntax_string
    readonly property color syntax_number: Themes.roles.syntax_number
    readonly property color syntax_comment: Themes.roles.syntax_comment
    readonly property color syntax_preprocessor: Themes.roles.syntax_preprocessor
    readonly property color syntax_heading: Themes.roles.syntax_heading

    // The branches of the graph, one after the other
    readonly property list<color> graph_lanes: Themes.roles.graph_lanes

    // Behind a modal dialog, and behind one that leaves the window usable
    readonly property color dim: Themes.roles.dim
    readonly property color dim_subtle: Themes.roles.dim_subtle
    readonly property real shadow_strength: Themes.roles.shadow_strength
}
