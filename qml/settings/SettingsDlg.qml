import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

Dialog {
    id: root
    x: (parent.width - width) / 3
    y: (parent.height - height) / 3
    width: Math.min(parent.width, 700)
    height: Math.min(parent.height - 10, 900)

    standardButtons: Dialog.Ok | Dialog.Cancel
    title: qsTr("Settings")

    ColumnLayout {
        anchors.margins: 20
        anchors.fill: parent
        TabBar {
            id: tab
            Layout.fillWidth: true

            TabButton {
                text: qsTr("General")
                width: implicitWidth
            }
        }

        StackLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            currentIndex: tab.currentIndex
            // Item {
            //     id: serverTab
            //     ServerSettings {id: server}
            // }
            Item {
                id: generalTab
                GeneralSettings {id: general}
            }
        }
    }

    onAccepted: {
        general.commit()
        close()
    }

    onRejected: {
        close()
    }
}

