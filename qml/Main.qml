import QtCore
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QVocalWriter

Window {
    id: root
    visible: true
    title: qsTr("QVocalWriter")

    // Which pane is active: 0 = Transcribe, 1 = Translate, 2 = Interactive
    property int currentView: 0
    property string statusText: appEngine.stateText

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
        anchors.margins: 10
        spacing: 10

        // MAIN AREA (sidebar + main pane)
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // LEFT VERTICAL NAV BAR
            Rectangle {
                id: sideBar
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                color: "#262626"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        text: qsTr("QVocalWriter")
                        font.bold: true
                        font.pointSize: 14
                        color: "#ffffff"
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Item { Layout.fillWidth: true; Layout.preferredHeight: 12 }

                    // Transcribe
                    Button {
                        Layout.fillWidth: true
                        text: "🎙  " + qsTr("Transcribe")
                        checkable: true
                        checked: root.currentView === 0
                        onClicked: root.currentView = 0
                    }

                    // Translate
                    Button {
                        Layout.fillWidth: true
                        text: "🌐  " + qsTr("Translate")
                        checkable: true
                        checked: root.currentView === 1
                        onClicked: root.currentView = 1
                    }

                    // Interactive
                    Button {
                        Layout.fillWidth: true
                        text: "💬  " + qsTr("Interactive")
                        checkable: true
                        checked: root.currentView === 2
                        onClicked: root.currentView = 2
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }
            }

            // RIGHT MAIN PANE
            StackLayout {
                id: mainStack
                Layout.margins: 10
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.currentView

                // 0: TRANSCRIBE – reuse RecordingPane
                RecordingPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }

                // 1: TRANSLATE (placeholder for now)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Translate view (coming soon)")
                        color: "gray"
                    }
                }

                // 2: INTERACTIVE (placeholder for now)
                ChatPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }

        // BOTTOM STATUS BAR
        Rectangle {
            id: statusBar
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#303030"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 12

                Text {
                    //Layout.fillWidth: true
                    text: root.statusText
                    color: "#f0f0f0"
                    elide: Text.ElideRight
                }

                Item {
                    Layout.fillWidth: true
                }

                RecordingLevel {
                    id: recordingLevel
                    Layout.preferredWidth: statusBar.width * 0.3
                    Layout.preferredHeight: statusBar.height - 12
                }
            }
        }
    }

    // BUSY SPINNER OVERLAY
    Rectangle {
        id: busyOverlay
        anchors.fill: parent
        visible: appEngine.isBusy
        enabled: visible
        z: 999
        color: "#60000000"   // dim background

        // Block all interaction beneath
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: appEngine.isBusy
            width: 64
            height: 64
        }
    }
}
