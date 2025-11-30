// qml/RecordingPane.qml
import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15

import QVocalWriter 1.0

Item {
    id: root
    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 12

        // --- Language selection ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Language")
                Layout.alignment: Qt.AlignVCenter
            }

            ComboBox {
                id: languageCombo
                Layout.fillWidth: true
                model: appEngine.languages
                currentIndex: appEngine.languageIndex

                onCurrentIndexChanged: {
                    if (currentIndex !== appEngine.languageIndex)
                        appEngine.languageIndex = currentIndex
                }

                enabled: appEngine.recordingState === AppEngine.Idle
                      || appEngine.recordingState === AppEngine.Error
            }
        }

        // --- Model size selection ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Model")
                Layout.alignment: Qt.AlignVCenter
            }

            ComboBox {
                id: modelCombo
                Layout.fillWidth: true
                model: appEngine.modelSizes
                currentIndex: appEngine.modelIndex

                onCurrentIndexChanged: {
                    if (currentIndex !== appEngine.modelIndex)
                        appEngine.modelIndex = currentIndex
                }

                enabled: appEngine.recordingState === AppEngine.Idle
                      || appEngine.recordingState === AppEngine.Error
            }
        }

        // --- Prepare button ---
        Button {
            id: prepareButton
            Layout.fillWidth: true
            text: qsTr("Prepare")
            enabled: appEngine.canPrepare
            onClicked: appEngine.prepare()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Model download progress:")
                Layout.alignment: Qt.AlignVCenter
                visible: modelDownloadBar.visible
            }

            ProgressBar {
                id: modelDownloadBar
                Layout.fillWidth: true

                // local state to hold progress
                property int bytesReceived: 0
                property int bytesTotal: 0

                from: 0
                to: 1
                value: bytesTotal > 0 ? bytesReceived / bytesTotal : 0

                // only show while a download is actually running
                visible: bytesTotal > 0 && bytesReceived < bytesTotal

                // Listen to the C++ signal from appEngine
                Connections {
                    target: appEngine
                    function onModelDownloadProgress(bytesReceivedArg, bytesTotalArg) {
                        modelDownloadBar.bytesReceived = bytesReceivedArg
                        modelDownloadBar.bytesTotal = bytesTotalArg
                    }
                }
            }
        }

        // --- Start/Stop button (same button, different label/color) ---
        Button {
            id: startStopButton
            Layout.fillWidth: true

            text: (appEngine.recordingState === AppEngine.Recording
                   || appEngine.recordingState === AppEngine.Processing)
                  ? qsTr("Stop")
                  : qsTr("Start")

            enabled: appEngine.canStart || appEngine.canStop

            background: Rectangle {
                // simple color cue; adjust to your style
                radius: 4
                color: (appEngine.recordingState === AppEngine.Recording
                        || appEngine.recordingState === AppEngine.Processing)
                       ? "#c62828" // red-ish
                       : "#2e7d32" // green-ish
            }

            onClicked: {
                if (appEngine.canStop) {
                    appEngine.stopRecording()
                } else if (appEngine.canStart) {
                    appEngine.startRecording()
                }
            }
        }

        // --- Status text ---
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: {
                switch (appEngine.recordingState) {
                case AppEngine.Preparing:
                    return qsTr("Preparing model…")
                case AppEngine.Ready:
                    return qsTr("Ready to record")
                case AppEngine.Recording:
                    return qsTr("Recording…")
                case AppEngine.Processing:
                    return qsTr("Processing recording…")
                case AppEngine.Error:
                    return qsTr("An error occurred. Please try again.")
                case AppEngine.Idle:
                default:
                    return qsTr("Idle")
                }
            }
        }

        // Optional: live text preview
        // Label {
        //     Layout.fillWidth: true
        //     wrapMode: Text.Wrap
        //     text: qsTr("Transcription: %1").arg(appEngine.lastText)
        // }
    }
}
