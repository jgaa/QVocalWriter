#pragma once

#include <QMediaDevices>
#include <QAudioDevice>

class AudioController : public QObject
{
    Q_OBJECT

public:
    explicit AudioController(QObject *parent = nullptr);

    const QList<QAudioDevice> inputDevices() const { return media_devices_.audioInputs(); }
    const QAudioDevice &currentInputDevice() const { return m_inputDevice; }

    void setInputDevice(const QAudioDevice &dev);
    void setInputDevice(int index);
    int getCurrentDeviceIndex() const;

signals:
    void inputDevicesChanged();
    void currentInputDeviceChanged();

private:
    void printDevices();

    QMediaDevices media_devices_;
    QAudioDevice  m_inputDevice;

    AudioController *self = this;
};
