// qml/ChatPane.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import QVocalWriter

Item {
    id: root
    implicitHeight: column.implicitHeight

    // local UI state
    //property string selectedModelName: modelCombo.currentText
    property bool hasModel: modelCombo.currentIndex >= 0
    property string lastChatError: ""

    function isUserActor(actor) {
        if (actor === undefined || actor === null)
            return false
        if (typeof actor === "string")
            return actor.toLowerCase() === "user" || actor.toLowerCase() === "human"
        if (typeof actor === "number")
            return actor === 0 // common convention: 0 = user
        return false
    }

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
                        model: appEngine.chatModels
                    }

                    Button {
                        text: qsTr("Prepare")
                        enabled: appEngine.canPrepareForChat && root.hasModel
                        onClicked: appEngine.prepareForChat()
                    }

                    Button {
                        text: qsTr("Clear")
                        enabled: chatView.count > 0 && !appEngine.isBusy
                        onClicked: clearChat()
                    }
                }

                DownloadProgress {
                    Layout.fillWidth: true
                }
            }
        }

        // --- Chat history ---
        // --- Chat history ---
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
                    visible: root.lastChatError.length > 0
                    text: qsTr("Error: ") + root.lastChatError
                    wrapMode: Text.WordWrap
                    color: palette.brightText
                    background: Rectangle { color: palette.mid; radius: 6; opacity: 0.9 }
                    padding: 8
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    ToolButton {
                        id: copyChatButton
                        text: qsTr("Copy chat")
                        icon.name: "edit-copy"
                        enabled: chatView.count > 0
                        onClicked: copyChatMenu.popup(copyChatButton)
                    }

                    Menu {
                        id: copyChatMenu
                        MenuItem { text: qsTr("Markdown"); onTriggered: chatView.model.copyAllToClipboard(ChatMessagesModel.Markdown) }
                        MenuItem { text: qsTr("JSON");     onTriggered: chatView.model.copyAllToClipboard(ChatMessagesModel.JSON) }
                    }
                }

                ListView {
                    id: chatView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 10
                    model: appEngine.chatMessages

                    delegate: ColumnLayout {
                        width: chatView.width
                        required property string actor
                        required property string message
                        required property bool completed
                        required property bool isUser
                        required property bool isAssistant
                        required property int index

                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                text: actor
                                font.bold: true
                                opacity: 0.8
                                Layout.fillWidth: true
                            }

                            ToolButton {
                                text: qsTr("Copy")
                                icon.name: "edit-copy"
                                onClicked: chatView.model.copyMessageToClipboard(index)
                            }
                        }

                        TextArea {
                            id: textItem
                            text: message
                            readOnly: true
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            background: null
                            Layout.fillWidth: true
                            textFormat: isAssistant ? TextEdit.MarkdownText : TextEdit.PlainText

                            Menu {
                                id: msgMenu
                                MenuItem {
                                    text: qsTr("Copy")
                                    onTriggered: chatView.model.copyMessageToClipboard(index)
                                }
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: msgMenu.popup()
                            }
                        }
                    }
                }
            }
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
