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

    function intOrDefault(value, fallback) {
        const parsed = parseInt(value)
        return Number.isFinite(parsed) ? parsed : fallback
    }

    function numberOrDefault(value, fallback) {
        const parsed = Number(value)
        return Number.isFinite(parsed) ? parsed : fallback
    }

    function commit() {
        settings.setValue("transcribe.vad.enabled", vadEnabled.checked)
        settings.setValue("transcribe.vad.speech_margin_db", numberOrDefault(vadSpeechMargin.text, 10.0))
        settings.setValue("transcribe.vad.min_speech_ms", intOrDefault(vadMinSpeechMs.text, 120))
        settings.setValue("transcribe.vad.min_silence_ms", intOrDefault(vadMinSilenceMs.text, 450))
        settings.setValue("transcribe.vad.noise_floor_alpha", numberOrDefault(vadNoiseAlpha.text, 0.02))
        settings.setValue("transcribe.vad.frame_ms", intOrDefault(vadFrameMs.text, 20))
        settings.setValue("transcribe.vad.preroll_ms", intOrDefault(vadPrerollMs.text, 120))
        settings.setValue("transcribe.vad.postroll_ms", intOrDefault(vadPostrollMs.text, 180))
        settings.setValue("transcribe.live.max_latency_ms", intOrDefault(liveMaxLatencyMs.text, 1500))
        settings.setValue("transcribe.post.skip_silence", postSkipSilence.checked)
        settings.sync()
    }

    GridLayout {
        width: root.width - 15
        rowSpacing: 4
        columns: width >= 350 ? 2 : 1

        Label {
            text: qsTr("Live and Post Transcription")
            font.bold: true
        }
        Item {}

        Item {}
        CheckBox {
            id: vadEnabled
            text: qsTr("Enable silence detection (VAD)")
            checked: settings.value("transcribe.vad.enabled", true)
        }

        Label { text: qsTr("Speech margin (dB)")}
        TextField {
            id: vadSpeechMargin
            Layout.fillWidth: true
            text: settings.value("transcribe.vad.speech_margin_db", 10.0).toString()
        }

        Label { text: qsTr("Min speech (ms)")}
        TextField {
            id: vadMinSpeechMs
            Layout.fillWidth: true
            text: settings.value("transcribe.vad.min_speech_ms", 120).toString()
        }

        Label { text: qsTr("Min silence (ms)")}
        TextField {
            id: vadMinSilenceMs
            Layout.fillWidth: true
            text: settings.value("transcribe.vad.min_silence_ms", 450).toString()
        }

        Label { text: qsTr("Noise floor alpha")}
        TextField {
            id: vadNoiseAlpha
            Layout.fillWidth: true
            text: settings.value("transcribe.vad.noise_floor_alpha", 0.02).toString()
        }

        Label { text: qsTr("Frame size (ms)")}
        TextField {
            id: vadFrameMs
            Layout.fillWidth: true
            text: settings.value("transcribe.vad.frame_ms", 20).toString()
        }

        Label { text: qsTr("Pre-roll (ms)")}
        TextField {
            id: vadPrerollMs
            Layout.fillWidth: true
            text: settings.value("transcribe.vad.preroll_ms", 120).toString()
        }

        Label { text: qsTr("Post-roll (ms)")}
        TextField {
            id: vadPostrollMs
            Layout.fillWidth: true
            text: settings.value("transcribe.vad.postroll_ms", 180).toString()
        }

        Label { text: qsTr("Live max latency (ms)")}
        TextField {
            id: liveMaxLatencyMs
            Layout.fillWidth: true
            text: settings.value("transcribe.live.max_latency_ms", 1500).toString()
        }

        Item {}
        CheckBox {
            id: postSkipSilence
            text: qsTr("Skip long silence in post transcription")
            checked: settings.value("transcribe.post.skip_silence", true)
        }

        Item {
            Layout.fillHeight: true
        }
        Item {}
    }
}
