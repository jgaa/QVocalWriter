import QtQuick
import QtQuick.Window
import Qt.labs.settings  // <--- Add this

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

    Text {
        anchors.centerIn: parent
        text: qsTr("Welcome to QVocalWriter")
        font.pixelSize: 24
    }
}
