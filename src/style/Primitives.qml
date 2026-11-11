pragma Singleton
import QtQuick

// The raw values of the design: the steps for spacing, radii, strokes and
// sizes. Nothing here says what a value is for. Colors are no
// primitives: they come from the theme in use (TokenInterface), as
// roles, and the control styles use those roles.
QtObject {
    id: root

    // Spacing in steps of 4 px; sp100 is 8 px
    component Spacing: QtObject {
        readonly property int sp025: 2
        readonly property int sp050: 4
        readonly property int sp075: 6
        readonly property int sp100: 8
        readonly property int sp125: 10
        readonly property int sp150: 12
        readonly property int sp200: 16
    }
    readonly property Spacing spacing: Spacing {}

    // Corners stay small: never more than 4 px, 2 px for small items in a
    // rounded container
    component Radius: QtObject {
        readonly property int r2: 2
        readonly property int r3: 3
        readonly property int r4: 4
    }
    readonly property Radius radius: Radius {}

    component Stroke: QtObject {
        readonly property int thin: 1
        readonly property int thick: 2
    }
    readonly property Stroke stroke: Stroke {}

    component Size: QtObject {
        readonly property int s2: 2
        readonly property int s6: 6
        readonly property int s8: 8
        readonly property int s14: 14
        readonly property int s16: 16
        readonly property int s18: 18
        readonly property int s20: 20
        readonly property int s22: 22
        readonly property int s24: 24
        readonly property int s28: 28
        readonly property int s32: 32
        readonly property int s40: 40
        readonly property int s48: 48
        readonly property int s56: 56
        readonly property int s64: 64
        readonly property int s100: 100
        readonly property int s120: 120
        readonly property int s128: 128
        readonly property int s160: 160
        readonly property int s200: 200
    }
    readonly property Size size: Size {}

    // Text sizes in pixels beside igitt's normal font
    component FontSize: QtObject {
        readonly property int small: 12
        readonly property int title: 18
    }
    readonly property FontSize fontSize: FontSize {}

    component FontWeight: QtObject {
        readonly property int regular: Font.Normal
        readonly property int medium: Font.Medium
        readonly property int semiBold: Font.DemiBold
        readonly property int bold: Font.Bold
    }
    readonly property FontWeight fontWeight: FontWeight {}

    // A color with another opacity
    function alpha(c: color, a: real): color {
        return Qt.rgba(c.r, c.g, c.b, a)
    }
}
