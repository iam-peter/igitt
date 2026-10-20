pragma Singleton

import QtQuick
import QtCore

// Preferences, kept between sessions.
QtObject {
    id: root

    readonly property list<string> defaultTrailerKeys: [
        "Task-number", "Fixes", "Pick-to", "Resolves", "Signed-off-by", "Co-Authored-By",
    ]

    readonly property Settings commit: Settings {
        category: "commit"

        property int titleLimit: 50
        property int bodyWidth: 72
        property bool wrapBody: true
        property bool enforceTitleLimit: false
        property var trailerKeys: Array.from(root.defaultTrailerKeys)
    }

    readonly property Settings fonts: Settings {
        category: "fonts"

        property string uiFamily: "Inter"
        property real uiSize: 10
        property string codeFamily: "JetBrains Mono"
        property real codeSize: 10
    }

    readonly property Settings diff: Settings {
        category: "diff"

        property bool ignoreWhitespace: false
        property bool wordDiff: true
        property bool highlightSyntax: true
    }

    readonly property Settings remote: Settings {
        category: "remote"

        property string pushMode: "auto" // auto, plain, gerrit, github or gitlab
        property string gerritBranch: ""
        // Self-hosted servers, comma-separated.
        property string githubHosts: ""
        property string gitlabHosts: ""
    }

    readonly property Settings graph: Settings {
        category: "graph"

        property bool showRemotes: true
        property bool showTags: true
        property bool dimMerges: false
        property bool showInitials: true
        property bool showMinimap: true
        property bool currentBranchOnly: false

        property bool showRefs: true
        property bool showGraph: true
        property bool showChanges: true
        property bool showAuthor: true
        property bool showDate: true
        property bool showSha: true

        property real refsWidth: 170
        property real graphWidth: 110
        property real changesWidth: 120
        property real authorWidth: 150
        property real dateWidth: 130
        property real shaWidth: 95
    }
}
