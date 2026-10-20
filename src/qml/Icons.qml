pragma Singleton

import QtQuick

// Lucide icons (https://lucide.dev, ISC license), drawn in white so they
// can be tinted to any color.
QtObject {
    function source(name: string): url {
        return Qt.resolvedUrl(`../icons/${name}.svg`)
    }
}
