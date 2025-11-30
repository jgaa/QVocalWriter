import QtCore
import QtQuick
import QtQuick.Window
import QtQuick.Layouts

Window {
    id: root
    visible: true
    title: qsTr("QVocalWriter")

    // Load saved size and position
    x: settings.windowX
    y: settings.windowY
    width: settings.windowWidth
    height: settings.windowHeight

    // Save size and position whenever changed
    onXChanged: settings.windowX = x
    onYChanged: settings.windowY = y
    onWidthChanged: settings.windowWidth = width
    onHeightChanged: settings.windowHeight = height

    Settings {
        id: settings
        property int windowX: 100     // Default fallback values
        property int windowY: 100
        property int windowWidth: 800
        property int windowHeight: 600
    }


    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20

        RecordingPane {
            Layout.preferredWidth: 400
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
