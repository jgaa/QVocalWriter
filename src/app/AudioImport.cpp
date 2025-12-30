#include <array>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstdint>

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QUrl>
#include <QtGlobal>   // qOverload
#include <QtConcurrent/QtConcurrentRun>

#include <qcorofuture.h>

#include "AudioImport.h"

#include "logging.h"

using namespace std;

std::ostream& operator << (std::ostream& os, AudioImport::State state) {
    static constexpr auto names = to_array<string_view>({
        string_view{"Created"},
        "Decoding",
        "Done",
        "Error"
    });

    return os << names.at(size_t(state));
}

namespace {
static inline float clamp1(float x) {
    return std::max(-1.0f, std::min(1.0f, x));
}

static void bufferToFloatMono(const QAudioBuffer& buf, std::vector<float>& monoOut)
{
    const QAudioFormat fmt = buf.format();
    const int ch = fmt.channelCount();
    const int frames = buf.frameCount();
    if (ch <= 0 || frames <= 0) return;

    monoOut.resize(size_t(frames));

    auto downmix = [&](auto sampleAtIndexToFloat) {
        for (int f = 0; f < frames; ++f) {
            float s = 0.0f;
            const int base = f * ch;
            for (int c = 0; c < ch; ++c) s += sampleAtIndexToFloat(base + c);
            s /= float(ch);
            monoOut[size_t(f)] = clamp1(s);
        }
    };

    switch (fmt.sampleFormat()) {
    case QAudioFormat::Int16: {
        const auto* p = buf.constData<qint16>();
        downmix([&](int i){ return float(p[i]) / 32768.0f; });
        break;
    }
    case QAudioFormat::Int32: {
        const auto* p = buf.constData<qint32>();
        downmix([&](int i){ return float(p[i]) / 2147483648.0f; });
        break;
    }
    case QAudioFormat::Float: {
        const auto* p = buf.constData<float>();
        downmix([&](int i){ return p[i]; });
        break;
    }
    case QAudioFormat::UInt8: {
        const auto* p = buf.constData<quint8>();
        // 8-bit unsigned PCM: 128 is “zero”
        downmix([&](int i){ return (float(p[i]) - 128.0f) / 128.0f; });
        break;
    }
    default:
        monoOut.clear(); // unsupported; you can add more if you ever see them
        break;
    }
}

class LinearResampler {
public:
    void reset(int inRate, int outRate) {
        inRate_ = inRate;
        outRate_ = outRate;
        pos_ = 0.0;
        haveLast_ = false;
        last_ = 0.0f;
    }

    // Append input samples (mono float) and produce output samples (mono float)
    void process(const float* in, size_t n, std::vector<float>& out) {
        if (inRate_ <= 0 || outRate_ <= 0 || n == 0) return;

        if (inRate_ == outRate_) {
            out.insert(out.end(), in, in + n);
            if (n) { last_ = in[n-1]; haveLast_ = true; }
            return;
        }

        const double step = double(inRate_) / double(outRate_); // input samples per output sample

        // We treat the input as preceded by last_ so interpolation at chunk boundary is continuous.
        auto sampleAt = [&](size_t idx) -> float {
            if (idx == 0) return haveLast_ ? last_ : in[0];
            return in[idx - 1];
        };

        // pos_ is in “input sample units”, where 0.0 refers to the boundary between last_ and in[0]
        // and 1.0 corresponds to in[0], 2.0 -> in[1], etc.
        const double maxPos = double(n); // up to n (exclusive for safe idx+1)
        while (pos_ + step <= maxPos) {
            const double x = pos_;
            const size_t i = size_t(std::floor(x));
            const double frac = x - double(i);

            const float a = sampleAt(i);
            const float b = (i + 1 <= n) ? sampleAt(i + 1) : sampleAt(n);
            out.push_back(a + float((b - a) * frac));

            pos_ += step;
        }

        // Keep pos_ small and carry remainder into next chunk
        pos_ -= double(n);

        last_ = in[n - 1];
        haveLast_ = true;
    }

    // Optional: call at end; for linear resampling we usually don’t need to flush.
    void flush(std::vector<float>&) {}

private:
    int inRate_{0};
    int outRate_{0};
    double pos_{0.0};
    bool haveLast_{false};
    float last_{0.0f};
};


} // anon ns

AudioImport::AudioImport(QObject *parent)
: QObject(parent)
{

}

std::span<const float> AudioImport::mono16kData() const noexcept
{
    assert(state() == State::Done);
    if (state() != State::Done) {
        LOG_WARN_N << "mono16kData() called but state is not Done";
        return {};
    }
    return samples_;
}

QString AudioImport::errorMessage() const noexcept {
    std::lock_guard lock(mutex_);
    return error_message_;
}

QCoro::Task<bool> AudioImport::decodeMediaFile(const QString &filePath)
{
    auto path = filePath;
    auto ok = co_await QtConcurrent::run([this, path]() -> bool {
        return decode(path);
    });

    co_return ok;
}

AudioImport::State AudioImport::state() const noexcept {
    std::lock_guard lock(mutex_);
    return state_;
}

bool AudioImport::decode(const QString &filePath)
{
    LOG_DEBUG_N << "Decoding audio file: " << filePath;
    setState(State::Decoding);

    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(filePath));

    bool ok = true;
    setErrorMessage("");
    samples_.clear();

    QEventLoop loop;

    std::vector<float> chunkMono;
    LinearResampler resampler;
    bool resampler_init = false;

    connect(&decoder, &QAudioDecoder::bufferReady, this, [&]() {
        const QAudioBuffer buf = decoder.read();
        const QAudioFormat fmt = buf.format();
        if (!fmt.isValid()) return;

        if (!resampler_init) {
            resampler.reset(fmt.sampleRate(), 16000);
            resampler_init = true;
        }

        bufferToFloatMono(buf, chunkMono);
        if (chunkMono.empty()) return;

        resampler.process(chunkMono.data(), chunkMono.size(), samples_);
    });

    connect(&decoder, &QAudioDecoder::finished, &loop, &QEventLoop::quit);

    connect(&decoder,
            qOverload<QAudioDecoder::Error>(&QAudioDecoder::error),
            this,
            [&](QAudioDecoder::Error) {
                setErrorMessage(decoder.errorString());
                ok = false;
                loop.quit();
            });

    decoder.start();
    loop.exec();

    setState(ok ? State::Done : State::Error);
    return ok;
}

void AudioImport::setErrorMessage(const QString &msg) noexcept {
    lock_guard lock(mutex_);
    error_message_ = msg;
}

void AudioImport::setState(State newState) noexcept
{
    {
        lock_guard lock(mutex_);
        if (newState == state_) {
            return;
        }

        state_ = newState;
    }
    LOG_DEBUG_N << "State changed to " << state_;
    emit stateChanged();
}
