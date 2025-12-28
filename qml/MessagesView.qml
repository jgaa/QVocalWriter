// qml/ChatPane.qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import QVocalWriter

Frame {
    id: root
    property var model: null
    padding: 8

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

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
            model: root.model

            delegate: ColumnLayout {
                width: chatView.width
                required property string actor
                required property string message
                required property bool completed
                required property bool isUser
                required property bool isAssistant
                required property int index
                required property double duration
                required property string modelId

                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: actor
                        font.bold: true
                        opacity: 0.8
                    }

                    Label {
                        text: modelId
                        visible: isAssistant
                    }

                    Label {
                        text: qsTr("durtation: %1s").arg(duration.toFixed(2))
                        visible: isAssistant && completed && duration > 0
                        font.italic: true
                    }

                    Item {
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
