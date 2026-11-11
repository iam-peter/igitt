# Themes

igitt's colors come from themes. A theme is a JSON file with a palette and
the color *roles* that igitt's controls, diffs and graph use, each one made
from that palette. igitt comes with these themes:

- Light and Dark, igitt's own
- [Dracula](https://draculatheme.com) and its light counterpart Alucard
- [Catppuccin](https://catppuccin.com) Latte, Frappé, Macchiato and Mocha

Settings › Theme picks a light and a dark theme, and whether igitt shows
the one that matches the system or always one of them.

## Your own themes

Put a `.json` file in the theme folder. Settings shows where that is, and
its button opens it:

| System  | Folder                              |
| ------- | ----------------------------------- |
| Linux   | `~/.config/igitt/themes`            |
| macOS   | `~/Library/Preferences/igitt/themes` |
| Windows | `%LOCALAPPDATA%\igitt\themes`       |

The file name without `.json` is the theme's id. igitt reads the folder
again whenever a file in it changes, so a theme can be written while igitt
shows it. What is wrong with a theme shows in Settings below the theme
choice, and on the terminal igitt was started from (as `igitt.theme`).

A theme only needs the roles it changes; everything else comes from the
theme below it. This one is Catppuccin Mocha with a pink accent:

```json
{
    "name": "Mocha, pink",
    "inherits": "catppuccin-mocha",
    "roles": {
        "primary_default": "pink",
        "stroke_focus": "pink"
    }
}
```

A file with the id of a built-in theme (`dracula.json`) takes its place in
the list and builds on it.

## The file

```json
{
    "name": "Nord",
    "dark": true,
    "inherits": "dark",
    "colors": {
        "polar_night_0": "#2e3440",
        "frost_2": "#81a1c1"
    },
    "roles": {
        "background_elevated": "polar_night_0",
        "primary_default": "frost_2",
        "dim": "polar_night_0@0.6"
    }
}
```

- **name** is what Settings lists. A file without one is skipped.
- **dark** says whether it is a dark theme, so whether Settings offers it
  as the light or the dark one. Without it the theme is as dark as the one
  below it.
- **inherits** is the id of the theme below it. Without it that is `dark`
  for a dark theme and `light` for any other.
- **colors** is the palette: names of your choice, each a `#rrggbb` or
  `#rrggbbaa` color.
- **roles** gives roles their values (see below). A color is a name from
  the palette or a `#rrggbb` or `#rrggbbaa` color. Either can take an
  opacity after an `@`, from 0 to 1: `"crust@0.6"`. As in CSS, the opacity
  in `#rrggbbaa` comes last.

The palette and the roles of a theme go over those of the theme below it,
and only then are the roles read. So a theme that only changes colors of
its palette recolors every role below it that uses them:

```json
{
    "name": "Mocha, pink",
    "inherits": "catppuccin-mocha",
    "colors": { "mauve": "#f5c2e7" }
}
```

A role whose value cannot be read keeps the one of the theme below it.

## Roles

Each role is a color, except `graph_lanes` (a list of colors) and
`shadow_strength` (a number).

### Text and the accent

| Role              | For                                                    |
| ----------------- | ------------------------------------------------------ |
| `text_default`    | Text and icons                                         |
| `text_muted`      | Hints, placeholders, secondary text                    |
| `text_inactive`   | Tabs that are not chosen                               |
| `text_on_primary` | Text on the accent: the default button, tooltips       |
| `text_on_danger`  | Text on a filled danger button                         |
| `text_link`       | Links and hunk headers                                 |
| `primary_default` | The accent: default buttons, checked boxes, text selections, tooltips, the minimap |

### What things are

| Role              | For                                                      |
| ----------------- | -------------------------------------------------------- |
| `danger_default`  | Errors, removed files and lines, failed checks           |
| `danger_fill`     | Filled danger buttons                                    |
| `success_default` | Added files and lines, approved reviews, "fold here"     |
| `warning_default` | Changed files, outdated reviews, "theirs" in a conflict  |
| `info_default`    | Renamed files, open reviews, "ours" in a conflict, the age of lines in the time-lapse view |
| `special_default` | Merged reviews, planned commits, plan mode               |
| `icon_folder`     | Folders in the file tree                                 |

### Surfaces

| Role                   | For                                               |
| ---------------------- | ------------------------------------------------- |
| `background_default`   | The window, tool bars, dialogs                    |
| `background_elevated`  | Lists, the diff, popups, menus                    |
| `background_muted`     | The tab bar                                       |
| `background_secondary` | File headers in a diff                            |
| `background_accent`    | Chosen rows, menu entries under the pointer       |
| `background_hover`     | Flat buttons under the pointer                    |
| `background_stripe`    | Every other row of a list                         |
| `selection_default`    | Chosen lines of a diff                            |
| `field_default`        | Text fields, combo boxes, the chosen tab          |
| `field_hover`          | Fields under the pointer                          |
| `outline_default`      | Outlined buttons, the default kind                |
| `outline_hover`        | Outlined buttons under the pointer                |

### Lines

| Role             | For                                    |
| ---------------- | -------------------------------------- |
| `stroke_subtle`  | Panels, separators, popups             |
| `stroke_field`   | The border of fields and check boxes   |
| `stroke_focus`   | Keyboard focus                         |
| `stroke_outline` | The border of outlined buttons         |
| `stroke_tab`     | The border of the chosen tab           |

### Controls

| Role                   | For                                 |
| ---------------------- | ----------------------------------- |
| `switch_thumb`         | A switch's thumb                    |
| `switch_thumb_checked` | A switch's thumb when it is on      |
| `scroll_thumb`         | Scroll bars                         |

### Diffs and code

| Role                  | For                                      |
| --------------------- | ---------------------------------------- |
| `diff_added`          | Behind added lines                       |
| `diff_removed`        | Behind removed lines                     |
| `diff_hunk`           | Behind hunk headers                      |
| `diff_added_word`     | Behind the words that changed in an added line   |
| `diff_removed_word`   | Behind the words that changed in a removed line  |
| `syntax_keyword`      | Keywords                                 |
| `syntax_type`         | Types                                    |
| `syntax_function`     | Functions                                |
| `syntax_string`       | Strings                                  |
| `syntax_number`       | Numbers                                  |
| `syntax_comment`      | Comments                                 |
| `syntax_preprocessor` | Preprocessor lines                       |
| `syntax_heading`      | Markdown headings                        |

The backgrounds of diff lines are best a little transparent (`@0.15`), so
the chosen line and the words that changed still show through.

### The rest

| Role              | For                                                       |
| ----------------- | --------------------------------------------------------- |
| `graph_lanes`     | The branches of the graph, in turn: `["blue", "peach"]`   |
| `dim`             | Over the window behind a dialog                           |
| `dim_subtle`      | The same, behind a dialog that leaves the window usable   |
| `shadow_strength` | How dark the shadow of popups is, from 0 to 1             |

The built-in themes in [src/style/themes](../src/style/themes) give every
role a value, `light.json` and `dark.json` without building on another.
