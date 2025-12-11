import QtQuick
import QtQuick.Layouts

import QVocalWriter

Rectangle {
    id: meter
    radius: height / 2
    color: "#202020"
    border.color: "#505050"
    border.width: 1
    clip: true


    // Filled part
    Rectangle {
        id: fill
        anchors.left : parent.left
        anchors.top : parent.top
        anchors.bottom : parent.bottom

        // Use appEngine.recordingLevel (0..1) horizontally
        width: parent.width * Math.max(0, Math.min(1, appEngine.recordingLevel))

        color: appEngine.recordingLevel < 0.6
                 ? "#4caf50"    // green
                 : appEngine.recordingLevel < 0.85
                     ? "#ffeb3b" // yellow
                     : "#f44336" // red

        Behavior on width {
            NumberAnimation {
                duration: 80
                easing.type: Easing.OutCubic
            }
        }
    }
}
