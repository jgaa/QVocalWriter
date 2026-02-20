#pragma once

#include <chrono>
#include <span>

#include <QIODevice>

#include "AudioRingBuffer.h"

class AudioCaptureDevice : public QIODevice
{
    Q_OBJECT
public:
    explicit AudioCaptureDevice(AudioRingBuffer *ring, int sampleRate, QObject *parent = nullptr);

    bool open(OpenMode mode) override;

    void close() override;

protected:
    qint64 readData(char *, qint64) override
    {
        assert(false);
        return 0;
    }

    qint64 writeData(const char *data, qint64 len) override;

signals:
    void recordingLevelUpdated(qreal level);

private:
    struct ChunkStats {
        float rms_dbfs = -120.0F;
        float peak = 0.0F;
    };

    struct VadConfig {
        float speech_margin_db = 10.0F;
        int min_speech_ms = 120;
        int min_silence_ms = 450;
        float noise_floor_alpha = 0.02F;
    };

    void prepareBuffer();
    void recalculateRecordingLevel(std::span<const qint16> samples);
    ChunkStats calculateChunkStats(std::span<const qint16> samples) const;
    bool updateVadState(const ChunkStats& stats, int chunkDurationMs);
    static int toDurationMs(int sampleCount, int sampleRate);

    AudioRingBuffer *m_ring;
    int sample_rate_ = 16000;
    unsigned int segment_ = 0;
    QByteArray audioBuffer_;
    std::chrono::steady_clock::time_point chunk_start_time_;
    std::once_flag first_write_flag_;
    qreal recording_level_{};
    VadConfig vad_config_{};
    bool vad_enabled_ = true;
    float noise_floor_dbfs_ = -70.0F;
    int speech_ms_accum_ = 0;
    int silence_ms_accum_ = 0;
    bool in_speech_ = false;
};
