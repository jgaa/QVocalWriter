// qml/RecordingPane.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs

import QVocalWriter

Item {
    id: root
    implicitHeight: column.implicitHeight

    function indexOfModel(model, name) {
        if (!model || !name)
            return -1

        for (let i = 0; i < model.length; ++i) {
            if (model[i] === name)
                return i
        }
        return -1
    }

    function nameAtIndex(model, index) {
        if (!model || index < 0 || index >= model.length)
            return ""
        return model[index]
    }


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

                enabled: appEngine.state === AppEngine.Idle
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

                enabled: appEngine.state === AppEngine.Idle
                      || appEngine.state === AppEngine.Error
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
                id: transcribeModelCombo
                Layout.fillWidth: true
                model: appEngine.transcribeModels
                currentIndex: -1

                Component.onCompleted: {
                    currentIndex = indexOfModel(
                        appEngine.transcribeModels,
                        appEngine.transcribeModelName
                    )
                }

                onCurrentIndexChanged: {
                    appEngine.transcribeModelName =
                        nameAtIndex(appEngine.transcribeModels, currentIndex)
                }

                Connections {
                    target: appEngine

                    function onTranscribeModelNameChanged() {
                        transcribeModelCombo.currentIndex =
                            indexOfModel(appEngine.transcribeModels,
                                         appEngine.transcribeModelName)
                    }

                    // Your model list NOTIFY signal
                    function onLanguageIndexChanged() {
                        transcribeModelCombo.currentIndex =
                            indexOfModel(appEngine.transcribeModels,
                                         appEngine.transcribeModelName)
                    }
                }
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
                model: appEngine.transcribeModels
                currentIndex: -1

                Component.onCompleted: {
                    currentIndex = indexOfModel(
                        appEngine.transcribeModels,
                        appEngine.transcribePostModelName
                    )
                }

                onCurrentIndexChanged: {
                    appEngine.transcribePostModelName =
                        nameAtIndex(appEngine.transcribeModels, currentIndex)
                }

                Connections {
                    target: appEngine

                    function onTranscribePostModelNameChanged() {
                        postModelCombo.currentIndex =
                            indexOfModel(appEngine.transcribeModels,
                                         appEngine.transcribePostModelName)
                    }

                    function onLanguageIndexChanged() {
                        postModelCombo.currentIndex =
                            indexOfModel(appEngine.transcribeModels,
                                         appEngine.transcribePostModelName)
                    }
                }
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

            DownloadProgress {
                Layout.fillWidth: true
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

            text: (appEngine.state === AppEngine.Recording
                   || appEngine.state === AppEngine.Processing)
                  ? qsTr("Stop")
                  : qsTr("Start")

            enabled: appEngine.canStart || appEngine.canStop

            background: Rectangle {
                // simple color cue; adjust to your style
                radius: 4
                color: (appEngine.state === AppEngine.Recording
                        || appEngine.state === AppEngine.Processing)
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
                enabled: appEngine.state == AppEngine.Done

                onClicked: {
                    saveTranscriptDialog.open()
                }
            }

            Button {
                text: qsTr("Reset")
                enabled: appEngine.state == AppEngine.Done
                      || appEngine.state == AppEngine.Error
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
