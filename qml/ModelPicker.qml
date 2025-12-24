// ModelPicker.qml
import QtQuick 6.10
import QtQuick.Controls 6.10
import QtQuick.Layouts 6.10

Control {
    id: root

    property var model: null
    signal selected(int index)

    readonly property int selectedIndex: (model ? model.selected : -1)

    contentItem: RowLayout {
        spacing: 8

        Button {
            id: currentButton
            Layout.fillWidth: true
            text: root.model.selectedName
            onClicked: pickerPopup.open()

            contentItem: RowLayout {
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: currentButton.text
                    elide: Text.ElideRight
                }
                Label { text: "\u25BE"; opacity: 0.7 } // ▼
            }
        }
    }

    Popup {
        id: pickerPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        x: currentButton.mapToItem(null, 0, 0).x
        y: currentButton.mapToItem(null, 0, currentButton.height + 4).y
        width: Math.max(420, currentButton.width)
        height: Math.min(520, root.Window ? root.Window.height * 0.7 : 520)

        background: Rectangle {
            radius: 10
            border.width: 1
            border.color: Qt.rgba(1,1,1,0.12)
            color: Qt.rgba(0.12,0.12,0.12,0.98)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10

            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search models…")
                //clearButtonEnabled: true
            }

            ListView {
                id: listView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.model
                ScrollBar.vertical: ScrollBar { }

                delegate: ItemDelegate {
                    id: rowDel
                    width: ListView.view.width
                    highlighted: (index === root.selectedIndex)

                    // Your roles as required properties
                    required property int index
                    required property string name
                    required property string id
                    required property int sizeMB
                    required property bool downloaded

                    // Simple filtering
                    visible: {
                        const q = searchField.text.trim().toLowerCase()
                        if (q.length === 0) return true
                        return name.toLowerCase().indexOf(q) >= 0 ||
                               id.toLowerCase().indexOf(q) >= 0
                    }
                    height: visible ? implicitHeight : 0

                    onClicked: {
                        if (root.model) root.model.selected = index
                        root.selected(index)
                        pickerPopup.close()
                    }

                    contentItem: RowLayout {
                        spacing: 10

                        Label {
                            text: downloaded ? "\u2713" : "\u2B07" // ✓ / ⬇
                            font.pixelSize: 16
                            opacity: downloaded ? 0.95 : 0.55
                            width: 18
                            horizontalAlignment: Text.AlignHCenter
                            Layout.alignment: Qt.AlignVCenter
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label {
                                    Layout.fillWidth: true
                                    text: name
                                    elide: Text.ElideRight
                                    font.pixelSize: 14
                                }

                                Label {
                                    text: sizeMB > 0 ? (sizeMB + " MB") : ""
                                    opacity: 0.7
                                    font.pixelSize: 12
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: id
                                opacity: 0.55
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }

                        // Future badges live here (favorite, times used, etc.)
                    }
                }
            }
        }
    }
}
