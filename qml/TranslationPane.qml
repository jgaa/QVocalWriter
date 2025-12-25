// qml/TranslationPane.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import QVocalWriter

Item {
    id: root
    implicitHeight: column.implicitHeight

    property bool hasModel: modelCombo.currentIndex >= 0
    property string lastTranslateError: ""

    function clearAll() {
        lastTranslateError = ""
        inputEdit.text = ""
        outputEdit.text = ""
        // optional: appEngine.resetTranslationState()
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: errorLabel.visible ? errorLabel.bottom : parent.top
        anchors.topMargin: errorLabel.visible ? 8 : 0
        spacing: 10

        // --- Model selection + Prepare ---
        GroupBox {
            title: qsTr("Translation model")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ModelPicker {
                        id: modelCombo
                        Layout.fillWidth: true
                        model: appEngine.translationModels
                    }

                    Button {
                        text: qsTr("Prepare")
                        enabled: appEngine.canPrepareForTranslate && root.hasModel
                        onClicked: appEngine.prepareForTranslation()
                    }

                    Button {
                        text: qsTr("Clear")
                        enabled: (!appEngine.isBusy) && (inputEdit.text.length > 0 || outputEdit.text.length > 0)
                        onClicked: clearAll()
                    }
                }

                DownloadProgress {
                    Layout.fillWidth: true
                }
            }
        }

        // --- Language selection ---
        GroupBox {
            title: qsTr("Languages")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    // Source
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label { text: qsTr("Source") }

                        ComboBox {
                            id: sourceLang
                            Layout.fillWidth: true
                            model: appEngine.sourceLanguages   // LanguagesModel*
                            textRole: "name"
                            valueRole: "code"

                            // LanguagesModel carries selection; keep QML simple:
                            currentIndex: appEngine.sourceLanguages.selected
                            onActivated: appEngine.sourceLanguages.selected = currentIndex
                        }
                    }

                    ToolButton {
                        text: qsTr("Swap")
                        icon.name: "view-refresh"
                        enabled: !appEngine.isBusy
                        onClicked: appEngine.swapTranslationLanguages()
                    }

                    // Target
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Label { text: qsTr("Target") }

                        ComboBox {
                            id: targetLang
                            Layout.fillWidth: true
                            model: appEngine.targetLanguages   // LanguagesModel*
                            textRole: "name"
                            valueRole: "code"

                            currentIndex: appEngine.targetLanguages.selected
                            onActivated: appEngine.targetLanguages.selected = currentIndex
                        }
                    }
                }
            }
        }

        // --- Output area ---
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 8

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    id: errorLabel
                    Layout.fillWidth: true
                    visible: root.lastTranslateError.length > 0
                    text: qsTr("Error: ") + root.lastTranslateError
                    wrapMode: Text.WordWrap
                    color: palette.brightText
                    background: Rectangle { color: palette.mid; radius: 6; opacity: 0.9 }
                    padding: 8
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Result")
                        opacity: 0.8
                    }

                    ToolButton {
                        text: qsTr("Copy")
                        icon.name: "edit-copy"
                        enabled: outputEdit.text.length > 0
                        onClicked: appEngine.copyTextToClipboard(outputEdit.text)
                    }
                }

                TextArea {
                    id: outputEdit
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    placeholderText: qsTr("Translation will appear here…")
                }
            }
        }

        // --- Input + Translate ---
        GroupBox {
            title: qsTr("Text")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                TextArea {
                    id: inputEdit
                    Layout.fillWidth: true
                    implicitHeight: 110
                    wrapMode: TextEdit.Wrap
                    placeholderText: qsTr("Paste or write text…  (Ctrl+Enter to translate)")

                    Keys.onPressed: (event) => {
                        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Return) {
                            translateButton.clicked()
                            event.accepted = true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    Button {
                        id: translateButton
                        text: qsTr("Translate")
                        enabled: !appEngine.isBusy && inputEdit.text.trim().length > 0
                        onClicked: {
                            const t = inputEdit.text.trim()
                            if (!t.length) return
                            root.lastTranslateError = ""
                            appEngine.translate(t) // new C++ invokable
                        }
                    }
                }
            }
        }
    }

    // --- Receive translation + errors ---
    Connections {
        target: appEngine
        ignoreUnknownSignals: true

        function onTranslationAvailable(text) {
            outputEdit.text = text
        }

        function onErrorOccurred(message) {
            root.lastTranslateError = message
        }
    }
}
