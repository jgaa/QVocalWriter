// qml/RecordingPane.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs

import QVocalWriter

Item {
    id: root
    implicitHeight: column.implicitHeight
    property bool canChangeSettings: appEngine.state === AppEngine.Idle

    ColumnLayout {
        id: column
        anchors.fill: parent
        spacing: 12

        // Source selection
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Input Source")
            }

            ComboBox  {
                id: inputSource
                enabled: root.canChangeSettings
                model: [
                    qsTr("Microphone")
                    , qsTr("File")
                ]
            }

            ComboBox {
                id: micSelection
                Layout.fillWidth: true
                model: appEngine.michrophones
                currentIndex: appEngine.currentMic
                visible: inputSource.currentIndex == AppEngine.Mic
                enabled: root.canChangeSettings

                onCurrentIndexChanged: {
                    if (currentIndex !== appEngine.currentMic)
                        appEngine.currentMic = currentIndex
                }
            }

            Button {
                id: fileSelectButton
                property string selectedFile: ""
                Layout.fillWidth: true
                text: qsTr("Select Audio File...")
                visible: inputSource.currentIndex === AppEngine.File
                enabled: root.canChangeSettings

                onClicked: {
                    srcFileDialog.open()
                }
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

                enabled: root.canChangeSettings
            }
        }

        // --- Model size selection ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: inputSource.currentIndex == AppEngine.Mic

            Label {
                text: qsTr("Live transcribe Model")
                Layout.alignment: Qt.AlignVCenter
            }

            ModelPicker {
                Layout.fillWidth: true
                model: appEngine.liveTranscribeModels
                enabled: root.canChangeSettings
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Post transcription Model")
                Layout.alignment: Qt.AlignVCenter
            }

            ModelPicker {
                Layout.fillWidth: true
                model: appEngine.postTranscribeModels
                enabled: root.canChangeSettings
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Document preparation model")
                Layout.alignment: Qt.AlignVCenter
            }

            ModelPicker {
                id: docPrepareModelPicker
                Layout.fillWidth: true
                model: appEngine.docPrepareModels
                enabled: root.canChangeSettings
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: docPrepareModelPicker.model.hasSelection

            Label {
                text: qsTr("Output style")
                Layout.alignment: Qt.AlignVCenter
            }

            ComboBox {
                id: rewriteStyleCombo
                Layout.fillWidth: true
                model: appEngine.rewriteStyle
                textRole: "name"
                //valueRole: "kind"
                currentIndex: model.selected
                onCurrentIndexChanged: model.selected = currentIndex
                enabled: root.canChangeSettings
            }

            ComboBox {
                id: socialMediaType
                editable: true
                visible: rewriteStyleCombo.model.isSocialMedia
                enabled: root.canChangeSettings
                model: [
                    "X/Twitter", "Linkedin", "Reddit", "Facebook",
                    "Instagram", "TikTok", qsTr("Work/intranet"), qsTr("Generic")
                ]

                function syncToCpp() {
                    rewriteStyleCombo.model.socialMedia = editable ? editText : currentText
                }

                // When user types (this is the important one)
                onEditTextChanged: syncToCpp()

                // When user selects an item
                onActivated: syncToCpp()      // or onCurrentIndexChanged if you prefer

                // When user presses Enter in the editor
                onAccepted: syncToCpp()
            }

            ComboBox {
                id: creativeWritingTarget
                editable: true
                visible: rewriteStyleCombo.model.isCreativeWriting
                enabled: root.canChangeSettings
                model: [
                    qsTr("Dialogue Scene"),
                    qsTr("Essay"),
                    qsTr("Fantasy"),
                    qsTr("Flash Fiction"),
                    qsTr("General"),
                    qsTr("Horror"),
                    qsTr("Journal Entry"),
                    qsTr("Memoir"),
                    qsTr("Mystery"),
                    qsTr("Novel Chapter"),
                    qsTr("Personal Essay"),
                    qsTr("Poem"),
                    qsTr("Romance"),
                    qsTr("Science Fiction"),
                    qsTr("Screenplay Scene"),
                    qsTr("Short Story"),
                    qsTr("Stage Play Scene"),
                ]

                function syncToCpp() {
                    rewriteStyleCombo.model.creativeWritingTarget = editable ? editText : currentText
                }

                // When user types (this is the important one)
                onEditTextChanged: syncToCpp()

                // When user selects an item
                onActivated: syncToCpp()      // or onCurrentIndexChanged if you prefer

                // When user presses Enter in the editor
                onAccepted: syncToCpp()
            }

        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: qsTr("Translate using ")
                Layout.alignment: Qt.AlignVCenter
            }

            ModelPicker {
                id: docTranslateModelPicker
                Layout.fillWidth: true
                enabled: root.canChangeSettings
                model: appEngine.docTranslateModels
            }


            Label {
                text: qsTr("to")
                visible: docTranslateModelPicker.model.hasSelection
            }

            ComboBox {
                id: sourceLang
                Layout.fillWidth: true
                model: appEngine.docTranslateLanguages   // LanguagesModel*
                textRole: "name"
                valueRole: "code"
                visible: docTranslateModelPicker.model.hasSelection
                enabled: root.canChangeSettings

                // LanguagesModel carries selection; keep QML simple:
                currentIndex: appEngine.docTranslateLanguages.selected
                onActivated: appEngine.docTranslateLanguages.selected = currentIndex
            }
        }

        // --- Prepare button ---
        Button {
            id: prepareButton
            Layout.fillWidth: true
            text: qsTr("Prepare")
            enabled: appEngine.canPrepareForTranscribe
            onClicked: appEngine.prepareForTranscribe()
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
                switch (inputSource.currentIndex) {
                    case AppEngine.Mic:
                        if (appEngine.canStop) {
                            appEngine.stopRecording()
                        } else if (appEngine.canStart) {
                            appEngine.startRecording()
                        }
                    break;
                    case AppEngine.File:
                        appEngine.transcribeFile()
                        break;
                }
            }
        }

        // // Multiline text field with wrapped text
        // ScrollView {
        //     Layout.fillWidth: true
        //     Layout.fillHeight: true
        //     // Always show vertical scrollbar
        //     ScrollBar.vertical.policy: ScrollBar.AlwaysOn
        //     TextArea {
        //         readOnly: true
        //         wrapMode: Text.Wrap
        //         text: appEngine.recordedText
        //         placeholderText: qsTr("Transcript will appear here");
        //     }
        // }

        MessagesView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: appEngine.transcribeMessages
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
                      || appEngine.state == AppEngine.Ready
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

    FileDialog {
        id: srcFileDialog
        title: qsTr("Select Audio File")
        fileMode: FileDialog.ExistingFile
        nameFilters: ["Audio Files (*.wav *.mp3 *.flac *.m4a)", "All Files (*)"]

        onAccepted: {
            console.log("Selected audio file:", selectedFile)
            fileSelectButton.selectedFile = selectedFile
            appEngine.setInputAudioFile(selectedFile)
        }
    }
}
