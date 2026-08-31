#include "PcmConverter.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace surr {

namespace {
constexpr double kPi = 3.14159265358979323846;

/// Frequência de corte do passa-baixa: um pouco abaixo de Nyquist do alvo
/// (8 kHz), com folga para o filtro simples de 2 polos não deixar passar
/// energia que voltaria rebatida.
constexpr double kCutoffHz = 7200.0;

int16_t clampToInt16(float value)
{
    const float scaled = value * 32767.0f;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= -32768.0f) return -32768;
    return static_cast<int16_t>(std::lrintf(scaled));
}
} // namespace

bool PcmConverter::InputFormat::isValid() const
{
    return sampleRate > 0 && channels > 0 && channels <= 8;
}

int PcmConverter::InputFormat::bytesPerSample() const
{
    switch (type) {
    case SampleType::Int16: return 2;
    case SampleType::Int32:
    case SampleType::Float32: return 4;
    }
    return 2;
}

PcmConverter::PcmConverter(const InputFormat &format) : m_format(format)
{
    m_passthrough = format.sampleRate == kTargetRate && format.channels == 1
        && format.type == SampleType::Int16;

    if (format.sampleRate > 0) {
        m_ratio = static_cast<double>(format.sampleRate) / kTargetRate;
        if (format.sampleRate % kTargetRate == 0) {
            m_decimation = format.sampleRate / kTargetRate;
        } else {
            m_decimation = 0; // razão fracionária: usa interpolação linear
        }

        // Passa-baixa RC de 1 polo aplicado duas vezes.
        const double dt = 1.0 / format.sampleRate;
        const double rc = 1.0 / (2.0 * kPi * kCutoffHz);
        m_alpha = dt / (rc + dt);
    }
}

bool PcmConverter::isPassthrough() const { return m_passthrough; }

void PcmConverter::reset()
{
    m_phase = 0.0;
    m_lp1 = m_lp2 = 0.0;
    m_partial.clear();
    m_lastPeak = 0.0f;
    m_hasPending = false;
}

float PcmConverter::readFrameMono(const char *frame) const
{
    const int stride = m_format.bytesPerSample();
    double sum = 0.0;

    for (int c = 0; c < m_format.channels; ++c) {
        const char *p = frame + c * stride;
        switch (m_format.type) {
        case SampleType::Int16:
            sum += qFromLittleEndian<qint16>(p) / 32768.0;
            break;
        case SampleType::Int32:
            sum += qFromLittleEndian<qint32>(p) / 2147483648.0;
            break;
        case SampleType::Float32: {
            float value = 0.0f;
            std::memcpy(&value, p, sizeof(float));
            sum += value;
            break;
        }
        }
    }
    // Média, não soma: somar dois canais idênticos estouraria em 2,0.
    return static_cast<float>(sum / m_format.channels);
}

QByteArray PcmConverter::convert(const char *data, qint64 size)
{
    if (size <= 0 || !m_format.isValid()) {
        return {};
    }

    if (m_passthrough) {
        // Ainda assim mede o pico, que alimenta a animação do overlay.
        float peak = 0.0f;
        const qint64 samples = size / 2;
        for (qint64 i = 0; i < samples; ++i) {
            const float v = qFromLittleEndian<qint16>(data + i * 2) / 32768.0f;
            peak = std::max(peak, std::fabs(v));
        }
        m_lastPeak = peak;
        return QByteArray(data, size);
    }

    // Junta o quadro parcial que sobrou da chamada anterior.
    QByteArray buffer;
    const char *cursor = data;
    qint64 available = size;
    if (!m_partial.isEmpty()) {
        buffer = m_partial;
        buffer.append(data, size);
        m_partial.clear();
        cursor = buffer.constData();
        available = buffer.size();
    }

    const int frameSize = m_format.bytesPerFrame();
    const qint64 frames = available / frameSize;
    const qint64 leftover = available - frames * frameSize;

    QByteArray out;
    out.reserve(static_cast<int>(frames / std::max(1.0, m_ratio)) * 2 + 4);

    float peak = 0.0f;

    for (qint64 i = 0; i < frames; ++i) {
        const float mono = readFrameMono(cursor + i * frameSize);
        peak = std::max(peak, std::fabs(mono));

        // Passa-baixa antes de reduzir a taxa.
        m_lp1 += m_alpha * (mono - m_lp1);
        m_lp2 += m_alpha * (m_lp1 - m_lp2);
        const float filtered = static_cast<float>(m_lp2);

        if (m_decimation > 0) {
            // Razão inteira: pega uma amostra a cada N.
            m_phase += 1.0;
            if (m_phase >= m_decimation) {
                m_phase -= m_decimation;
                const int16_t s = clampToInt16(filtered);
                out.append(reinterpret_cast<const char *>(&s), 2);
            }
        } else {
            // Razão fracionária: interpolação linear entre vizinhos.
            m_phase += 1.0;
            while (m_phase >= m_ratio) {
                m_phase -= m_ratio;
                const float interpolated = m_hasPending
                    ? m_pendingSample + (filtered - m_pendingSample)
                          * static_cast<float>(1.0 - m_phase / m_ratio)
                    : filtered;
                const int16_t s = clampToInt16(interpolated);
                out.append(reinterpret_cast<const char *>(&s), 2);
            }
            m_pendingSample = filtered;
            m_hasPending = true;
        }
    }

    if (leftover > 0) {
        m_partial = QByteArray(cursor + frames * frameSize, static_cast<int>(leftover));
    }

    m_lastPeak = peak;
    return out;
}

} // namespace surr
