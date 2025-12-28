// qml/ChatPane.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import QVocalWriter

Item {
    id: root
    implicitHeight: column.implicitHeight

    // local UI state
    property bool hasModel: modelCombo.currentIndex >= 0
    property string lastChatError: ""

    function clearChat() {
        root.lastChatError = ""
        // Starts a fresh conversation in C++ (clears the message list there)
        appEngine.startChatConversation("default")
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
            title: qsTr("Chat model")
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
                        enabled: appEngine.canChangeConfig
                        model: appEngine.chatModels
                    }

                    Button {
                        text: qsTr("Prepare")
                        enabled: appEngine.canPrepareforChat
                        onClicked: appEngine.prepareForChat()
                    }

                    Button {
                        text: qsTr("Clear")
                        enabled: !appEngine.canChangeConfig
                        onClicked: clearChat()
                    }
                }

                DownloadProgress {
                    Layout.fillWidth: true
                }
            }
        }

        Label {
            id: errorLabel
            Layout.fillWidth: true
            visible: root.lastChatError.length > 0
            text: qsTr("Error: ") + root.lastChatError
            wrapMode: Text.WordWrap
            color: palette.brightText
            background: Rectangle { color: palette.mid; radius: 6; opacity: 0.9 }
            padding: 8
        }


        // --- Chat history ---
        MessagesView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: appEngine.chatMessages
        }
        // --- Prompt box + Send ---
        GroupBox {
            title: qsTr("Prompt")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                TextArea {
                    id: promptEdit
                    Layout.fillWidth: true
                    implicitHeight: 90
                    wrapMode: TextEdit.Wrap
                    placeholderText: qsTr("Write your prompt…  (Ctrl+Enter to send)")

                    Keys.onPressed: (event) => {
                        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Return) {
                            sendButton.clicked()
                            event.accepted = true
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        id: sendButton
                        text: qsTr("Send")
                        enabled: !appEngine.isBusy && promptEdit.text.trim().length > 0

                        onClicked: {
                            const p = promptEdit.text.trim()
                            if (!p.length)
                                return

                            root.lastChatError = ""
                            promptEdit.text = ""
                            appEngine.chatPrompt(p)
                        }
                    }
                }
            }
        }

        // --- Keep view pinned to bottom when the C++ model updates ---
        Connections {
            target: appEngine.chatMessages
            ignoreUnknownSignals: true

            function onRowsInserted(parent, first, last) { chatView.positionViewAtEnd() }
            function onDataChanged(topLeft, bottomRight, roles) { chatView.positionViewAtEnd() }
            function onModelReset() { chatView.positionViewAtEnd() }
        }

        // Optional: surface errors from AppEngine without mutating the chat model in QML
        Connections {
            target: appEngine
            ignoreUnknownSignals: true

            function onErrorOccurred(message) {
                root.lastChatError = message
            }
        }
    }
}
