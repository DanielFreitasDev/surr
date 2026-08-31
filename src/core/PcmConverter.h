#pragma once

#include <QByteArray>
#include <vector>

namespace surr {

/**
 * Converte o PCM que o dispositivo entrega para o que o STT quer:
 * mono, 16 bits com sinal, 16 kHz.
 *
 * Existe porque nenhum microfone desta máquina oferece o alvo nativamente —
 * são 48 kHz ou 32 kHz, sempre estéreo. Como ambos são múltiplos inteiros de
 * 16 kHz, a conversão é decimação por inteiro depois de um passa-baixa; o
 * caminho de razão arbitrária existe para hardware que não vimos.
 *
 * Trabalha em fluxo: mantém o estado do filtro e a fase entre chamadas, para
 * que blocos consecutivos não produzam clique nas emendas.
 */
class PcmConverter
{
public:
    enum class SampleType { Int16, Int32, Float32 };

    struct InputFormat {
        int sampleRate = 48000;
        int channels = 2;
        SampleType type = SampleType::Int16;

        bool isValid() const;
        int bytesPerSample() const;
        int bytesPerFrame() const { return channels * bytesPerSample(); }
    };

    static constexpr int kTargetRate = 16000;

    explicit PcmConverter(const InputFormat &format);

    /// True quando a entrada já é o alvo e a conversão é cópia direta.
    bool isPassthrough() const;

    /**
     * Converte @p input e devolve PCM mono s16 a 16 kHz.
     * Bytes incompletos de um quadro ficam guardados para a próxima chamada.
     */
    QByteArray convert(const char *data, qint64 size);
    QByteArray convert(const QByteArray &input)
    {
        return convert(input.constData(), input.size());
    }

    /// Pico absoluto (0..1) do último bloco convertido.
    float lastPeak() const { return m_lastPeak; }

    void reset();

private:
    /// Lê um quadro e devolve a média dos canais, normalizada em -1..1.
    float readFrameMono(const char *frame) const;

    InputFormat m_format;
    bool m_passthrough = false;

    // Razão de decimação: inteira quando a taxa é múltipla de 16 kHz.
    int m_decimation = 1;
    double m_ratio = 1.0;
    double m_phase = 0.0;

    // Passa-baixa de 2 polos antes de decimar. Sem ele, tudo acima de 8 kHz
    // volta rebatido como ruído na banda da voz.
    double m_lp1 = 0.0;
    double m_lp2 = 0.0;
    double m_alpha = 1.0;

    QByteArray m_partial;   ///< quadro incompleto entre chamadas
    float m_lastPeak = 0.0f;
    float m_pendingSample = 0.0f;
    bool m_hasPending = false;
};

} // namespace surr
