// qml/ChatPane.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import QVocalWriter

Item {
    id: root
    implicitHeight: column.implicitHeight

    // local UI state
    property string selectedModelName: modelCombo.currentText
    property int streamingAssistantIndex: -1

    function appendUserMessage(text) {
        chatModel.append({ sender: "user", text: text })
        chatView.positionViewAtEnd()
    }

    function beginAssistantMessage() {
        chatModel.append({ sender: "assistant", text: "" })
        streamingAssistantIndex = chatModel.count - 1
        chatView.positionViewAtEnd()
    }

    function updateAssistantPartial(text) {
        if (streamingAssistantIndex < 0 || streamingAssistantIndex >= chatModel.count)
            beginAssistantMessage()
        chatModel.setProperty(streamingAssistantIndex, "text", text)
        chatView.positionViewAtEnd()
    }

    function finalizeAssistant(text) {
        // If final arrives without any partials, still create a message.
        if (streamingAssistantIndex < 0 || streamingAssistantIndex >= chatModel.count)
            beginAssistantMessage()
        chatModel.setProperty(streamingAssistantIndex, "text", text)
        streamingAssistantIndex = -1
        chatView.positionViewAtEnd()
    }

    function clearChat() {
        chatModel.clear()
        streamingAssistantIndex = -1
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
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

                    ComboBox {
                        id: modelCombo
                        Layout.fillWidth: true
                        model: appEngine.chatModels
                    }

                    Button {
                        text: qsTr("Prepare")
                        enabled: appEngine.canPrepare && selectedModelName.length > 0
                        onClicked: appEngine.prepareForChat(selectedModelName)
                    }

                    Button {
                        text: qsTr("Clear")
                        enabled: chatModel.count > 0 && !appEngine.isBusy
                        onClicked: clearChat()
                    }
                }

                // --- Download progress (reuses your signal) ---
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        id: downloadLabel
                        property string name: ""
                        text: name.length ? qsTr("Downloading " + name) : ""
                        visible: downloadBar.visible
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ProgressBar {
                        id: downloadBar
                        Layout.fillWidth: true

                        property int bytesReceived: 0
                        property int bytesTotal: 0

                        from: 0
                        to: 1
                        value: bytesTotal > 0 ? bytesReceived / bytesTotal : 0
                        visible: bytesTotal > 0 && bytesReceived < bytesTotal

                        Connections {
                            target: appEngine
                            function onDownloadProgress(name, bytesReceivedArg, bytesTotalArg) {
                                downloadLabel.name = name
                                downloadBar.bytesReceived = bytesReceivedArg
                                downloadBar.bytesTotal = bytesTotalArg
                            }
                        }
                    }
                }
            }
        }

        // --- Chat history ---
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 8

            ListModel { id: chatModel }

            ListView {
                id: chatView
                anchors.fill: parent
                clip: true
                spacing: 10
                model: chatModel

                delegate: Item {
                    width: chatView.width
                    implicitHeight: bubble.implicitHeight

                    Rectangle {
                        id: bubble
                        width: Math.min(chatView.width * 0.92, textItem.implicitWidth + 28)
                        x: sender === "user" ? chatView.width - width : 0
                        radius: 10
                        opacity: 0.95
                        border.width: 1

                        // Don’t hardcode colors; keep it theme-friendly
                        color: sender === "user" ? palette.base : palette.alternateBase
                        border.color: palette.mid

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 6

                            Label {
                                text: sender === "user" ? qsTr("You") : qsTr("Assistant")
                                font.bold: true
                                opacity: 0.8
                            }

                            TextArea {
                                id: textItem
                                text: model.text
                                readOnly: true
                                wrapMode: TextEdit.Wrap
                                selectByMouse: true
                                background: null
                                Layout.fillWidth: true
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

                    Label {
                        text: appEngine.stateText
                        Layout.fillWidth: true
                        elide: Label.ElideRight
                        opacity: 0.85
                    }

                    BusyIndicator {
                        running: appEngine.isBusy
                        visible: running
                    }

                    Button {
                        id: sendButton
                        text: qsTr("Send")
                        enabled: !appEngine.isBusy && promptEdit.text.trim().length > 0

                        onClicked: {
                            const p = promptEdit.text.trim()
                            if (!p.length)
                                return

                            appendUserMessage(p)
                            beginAssistantMessage()

                            promptEdit.text = ""
                            appEngine.chatPrompt(p)
                        }
                    }
                }
            }
        }

        // --- Stream + final text wiring ---
        Connections {
            target: appEngine
            ignoreUnknownSignals: true

            function onPartialTextAvailable(text) {
                updateAssistantPartial(text)
            }

            // If your C++ exposes this signal, we use it; otherwise it’s ignored.
            function onFinalTextAvailable(text) {
                finalizeAssistant(text)
            }

            function onErrorOccurred(message) {
                chatModel.append({ sender: "assistant", text: qsTr("Error: ") + message })
                streamingAssistantIndex = -1
                chatView.positionViewAtEnd()
            }
        }
    }
}
