// DownloadProgress.qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    spacing: 8

    // Public API (optional overrides)
    property string name: ""
    property real ratio: 0.0        // 0..1
    property bool active: false     // whether we should show the UI

    function reset() {
        name = ""
        ratio = 0.0
        active = false
    }

    Label {
        id: downloadLabel
        Layout.alignment: Qt.AlignVCenter
        text: root.name.length ? qsTr("Downloading " + root.name) : ""
        visible: downloadBar.visible
        elide: Text.ElideRight
        Layout.fillWidth: false
    }

    ProgressBar {
        id: downloadBar
        Layout.fillWidth: true

        from: 0
        to: 1
        value: Math.min(1.0, Math.max(0.0, root.ratio))

        // Visible while active and not completed
        visible: root.active && root.name.length > 0 && root.ratio < 1.0

        // Helps avoid the bar collapsing to 0 height in some layouts/styles
        implicitHeight: 10
    }

    Connections {
        target: appEngine

        function onDownloadProgressRatio(nameArg, ratioArg) {
            root.name = nameArg
            root.ratio = ratioArg
            root.active = true

            // Auto-hide after completion (optional)
            if (root.ratio >= 1.0) {
                root.active = false
                root.name = ""
                root.ratio = 0.0
            }
        }
    }
}
