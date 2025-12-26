import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtCore

ScrollView {
    id: root
    anchors.fill: parent

    Settings {
        id: settings
    }

    function commit() {
        settings.setValue("logging/path", logPath.text)
        settings.setValue("logging/level", logLevelFile.currentIndex.toString())
        settings.setValue("logging/applevel", logLevelApp.currentIndex.toString())
        settings.setValue("logging/prune", prune.checked ? "true" : "false")
        settings.sync()
    }

    GridLayout {
        width: root.width - 15
        rowSpacing: 4
        columns: width >= 350 ? 2 : 1

        Label {
            text: qsTr("Log Level\n(Application)")
            visible: logLevelApp.visible
        }
        ComboBox {
            id: logLevelApp

            visible: Qt.platform.os === "linux"

            currentIndex: parseInt(settings.value("logging/applevel"))
            Layout.fillWidth: true
            model: [
                qsTr("Disabled"),
                qsTr("Error"),
                qsTr("Warning"),
                qsTr("Notice"),
                qsTr("Info"),
                qsTr("Debug"),
                qsTr("Trace")]
        }

        Label { text: qsTr("Log Level\n(File)")}
        ComboBox {
            id: logLevelFile
            currentIndex: parseInt(settings.value("logging/level"))
            Layout.fillWidth: true
            model: [
                qsTr("Disabled"),
                qsTr("Error"),
                qsTr("Warning"),
                qsTr("Notice"),
                qsTr("Info"),
                qsTr("Debug"),
                qsTr("Trace")]
        }

        Label { text: qsTr("Logfile")}
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: logPath.implicitHeight + 16
            radius: 6
            border.width: 1
            border.color: palette.mid
            color: palette.base

            TextInput {
                id: logPath
                anchors.fill: parent
                anchors.margins: 8
                verticalAlignment: TextInput.AlignVCenter
                color: palette.text
                text: settings.value("logging/path", "")
            }
        }
        Item {}
        CheckBox {
            id: prune
            text: qsTr("Prune log-file when starting")
            checked: settings.value("logging/prune") === "true"
        }
        Item {
            Layout.fillHeight: true
        }
    }
}
