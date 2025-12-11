// qml/RecordingPane.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs

import QVocalWriter

Item {
    id: root
    implicitHeight: column.implicitHeight

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 12

        // Microphone selection
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Microphone")
                Layout.alignment: Qt.AlignVCenter
            }

            ComboBox {
                id: languageCombo
                Layout.fillWidth: true
                model: appEngine.michrophones
                currentIndex: appEngine.currentMic

                onCurrentIndexChanged: {
                    if (currentIndex !== appEngine.currentMic)
                        appEngine.currentMic = currentIndex
                }

                enabled: appEngine.recordingState === AppEngine.Idle
            }
        }


        // --- Language selection ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Language")
                Layout.alignment: Qt.AlignVCenter
            }

            ComboBox {
                id: micCombo
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
                text: qsTr("Recording Model")
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
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Post processing Model")
                Layout.alignment: Qt.AlignVCenter
            }

            ComboBox {
                id: postModelCombo
                Layout.fillWidth: true
                model: appEngine.modelSizes
                currentIndex: appEngine.postModelIndex

                onCurrentIndexChanged: {
                    if (currentIndex !== appEngine.postModelIndex)
                        appEngine.postModelIndex = currentIndex
                }

                enabled: appEngine.recordingState === AppEngine.Idle

            }
        }


        // --- Prepare button ---
        Button {
            id: prepareButton
            Layout.fillWidth: true
            text: qsTr("Prepare")
            enabled: appEngine.canPrepare
            onClicked: appEngine.prepareForRecording()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Pre-model download progress:")
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                id: downloadLabel
                property string name: ""
                text: qsTr("Downloading " + name)
                visible: downloadBar.visible
            }

            ProgressBar {
                id: downloadBar
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
                    function onDownloadProgress(name, bytesReceivedArg, bytesTotalArg) {
                        downloadBar.bytesReceived = bytesReceivedArg
                        downloadBar.bytesTotal = bytesTotalArg
                        downloadLabel.name = name
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

        // Multiline text field with wrapped text
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Always show vertical scrollbar
            ScrollBar.vertical.policy: ScrollBar.AlwaysOn
            TextArea {
                readOnly: true
                wrapMode: Text.Wrap
                text: appEngine.recordedText
                placeholderText: qsTr("Transcript will appear here");
            }
        }

        RowLayout {
            spacing: 8

            Button {
                text: qsTr("Save Transcript")
                enabled: appEngine.recordingState == AppEngine.Done

                onClicked: {
                    saveTranscriptDialog.open()
                }
            }

            Button {
                text: qsTr("Reset")
                enabled: appEngine.recordingState == AppEngine.Done
                      || appEngine.recordingState == AppEngine.Error
                onClicked: {
                    appEngine.reset();
                }
            }
        }
    }

    FileDialog {
        id: saveTranscriptDialog
        title: qsTr("Save Transcript")
        fileMode: FileDialog.SaveFile
        nameFilters: ["Text Files (*.txt)", "Markdown (*.md)", "All Files (*)"]
        defaultSuffix: "txt"

        onAccepted: {
            console.log("Saving to:", selectedFile)

            // Call into C++ to save
            appEngine.saveTranscriptToFile(selectedFile)
        }
    }
}
