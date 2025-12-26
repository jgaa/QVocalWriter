// AboutDialog.qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    modal: true
    focus: true
    title: qsTr("About QVocalWriter")

    // Size: sensible defaults for desktop; adapts to parent if you set root.parent
    width: Math.min(720, (parent ? parent.width : 900) * 0.9)
    height: Math.min(640, (parent ? parent.height : 700) * 0.9)

    standardButtons: Dialog.Close

    // If you want a custom footer instead of standardButtons:
    // footer: DialogButtonBox { standardButtons: DialogButtonBox.Close }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        padding: 16

        // ScrollView provides its own Flickable. Put content inside it:
        contentItem: Flickable {
            clip: true
            contentWidth: width
            contentHeight: col.implicitHeight

            ColumnLayout {
                id: col
                width: parent.width
                spacing: 12

                // Optional header block
                Label {
                    text: qsTr("QVocalWriter")
                    font.pixelSize: 22
                    font.bold: true
                    Layout.fillWidth: true
                }

                // Markdown body
                TextArea {
                    id: md
                    Layout.fillWidth: true
                    width: col.width

                    textFormat: Text.MarkdownText
                    text: appEngine.aboutText();
                    readOnly: true

                    wrapMode: Text.WordWrap

                    // Let links work
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }

                // Spacer at bottom so the last line isn't flush
                Item { Layout.fillWidth: true; height: 8 }
            }
        }
    }
}
