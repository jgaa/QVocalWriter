import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 800
    height: 600
    visible: true
    title: qsTr("QVocalWriter")

    Text {
        anchors.centerIn: parent
        text: qsTr("Welcome to QVocalWriter")
        font.pixelSize: 24
    }
}
