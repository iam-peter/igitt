import QtQuick
import QtQuick.Templates as T

T.ProgressBar {
    id: control

    property list<Style> styles

    StyleCascade {
        id: _style
        base: [ProgressBarStyle.base]
        styles: control.styles
    }
    StyleCascade {
        id: _trackStyle
        parentCascade: _style
        part: StylePart.Track
    }
    StyleCascade {
        id: _progressStyle
        parentCascade: _style
        part: StylePart.Progress
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    contentItem: Item {
        implicitWidth: _style.width
        implicitHeight: _style.height
        clip: true
        Rectangle {
            width: control.indeterminate ? parent.width / 4 : control.visualPosition * parent.width
            height: parent.height
            radius: _style.radius.value
            color: _progressStyle.background
            NumberAnimation on x {
                running: control.indeterminate && control.visible
                loops: Animation.Infinite
                from: -control.width / 4
                to: control.width
                duration: 1400
            }
        }
    }

    background: Rectangle {
        implicitWidth: _style.width
        implicitHeight: _style.height
        y: (control.height - height) / 2
        height: _style.height
        radius: _style.radius.value
        color: _trackStyle.background
    }
}
