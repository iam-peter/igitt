import QtQuick
import QtQuick.Controls

// A ScrollView whose content stops at its edges instead of overshooting;
// ScrollView leaves its Flickable's default bounds behavior in place.
ScrollView {
    Component.onCompleted: {
        if (contentItem instanceof Flickable) {
            contentItem.boundsBehavior = Flickable.StopAtBounds
            contentItem.boundsMovement = Flickable.StopAtBounds
        }
    }
}
