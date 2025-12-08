#include "AudioController.h"
#include "logging.h"


AudioController::AudioController(QObject *parent)
    : QObject(parent), m_inputDevice{QMediaDevices::defaultAudioInput()}
{
    LOG_DEBUG_N << "Available audio input devices: ";
    printDevices();

    // Optional: react to devices added/removed
    connect(&media_devices_, &QMediaDevices::audioInputsChanged,
            this, [&]{
        LOG_INFO_N << "Audio input devices changed " << media_devices_.audioInputs().size();
        printDevices();
        emit inputDevicesChanged();
    });
}

void AudioController::setInputDevice(const QAudioDevice &dev) {
    m_inputDevice = dev;
    LOG_INFO_N << "Current audio input device changed to " << dev.description();
    emit currentInputDeviceChanged();
}

void AudioController::printDevices()
{
    auto ix = 0u;
    for(const auto &dev : media_devices_.audioInputs()) {
        const bool is_current = (dev.id() == m_inputDevice.id());
        LOG_DEBUG_N << "  #" << ix << (is_current ? " * " : " : ") << dev.description();
        ++ix;
    }
}
