#include "PcmConverter.h"

#include <QTest>
#include <QtEndian>

#include <cmath>

using namespace surr;
using SampleType = PcmConverter::SampleType;

/// Nenhum microfone desta máquina entrega 16 kHz mono, então este caminho
/// roda em TODA gravação. Um erro aqui é ruído em cima da voz do usuário.
class PcmConverterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void passthroughWhenAlreadyTarget();
    void decimatesRealDeviceRates_data();
    void decimatesRealDeviceRates();
    void downmixesStereoToMono();
    void handlesSplitFramesAcrossCalls();
    void measuresPeak();
    void clampsWithoutWrapping();
    void supportsInt32AndFloat_data();
    void supportsInt32AndFloat();

private:
    /// Seno de @p freqHz gerado no formato pedido, @p frames quadros.
    static QByteArray tone(double freqHz, int frames, const PcmConverter::InputFormat &fmt,
                           double amplitude = 0.5)
    {
        QByteArray out;
        for (int i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / fmt.sampleRate;
            const double v = amplitude * std::sin(2.0 * M_PI * freqHz * t);
            for (int c = 0; c < fmt.channels; ++c) {
                switch (fmt.type) {
                case SampleType::Int16: {
                    qint16 s = static_cast<qint16>(v * 32767);
                    char b[2]; qToLittleEndian(s, b); out.append(b, 2);
                    break;
                }
                case SampleType::Int32: {
                    qint32 s = static_cast<qint32>(v * 2147483647.0);
                    char b[4]; qToLittleEndian(s, b); out.append(b, 4);
                    break;
                }
                case SampleType::Float32: {
                    float s = static_cast<float>(v);
                    out.append(reinterpret_cast<const char *>(&s), 4);
                    break;
                }
                }
            }
        }
        return out;
    }
};

void PcmConverterTest::passthroughWhenAlreadyTarget()
{
    PcmConverter c({16000, 1, SampleType::Int16});
    QVERIFY(c.isPassthrough());

    const QByteArray in = tone(440, 1600, {16000, 1, SampleType::Int16});
    QCOMPARE(c.convert(in), in); // cópia byte a byte, sem reprocessar
}

void PcmConverterTest::decimatesRealDeviceRates_data()
{
    QTest::addColumn<int>("rate");
    QTest::addColumn<int>("channels");

    // Exatamente os formatos que os microfones desta máquina expõem.
    QTest::newRow("USB-C 48k estéreo") << 48000 << 2;
    QTest::newRow("webcam C925e 32k estéreo") << 32000 << 2;
    QTest::newRow("interno 48k estéreo") << 48000 << 2;
    QTest::newRow("48k mono") << 48000 << 1;
    QTest::newRow("44,1k estéreo (razão fracionária)") << 44100 << 2;
}

void PcmConverterTest::decimatesRealDeviceRates()
{
    QFETCH(int, rate);
    QFETCH(int, channels);

    const PcmConverter::InputFormat fmt{rate, channels, SampleType::Int16};
    PcmConverter c(fmt);
    QVERIFY(!c.isPassthrough());

    const int frames = rate; // 1 segundo
    const QByteArray out = c.convert(tone(440, frames, fmt));

    // 1 s de entrada tem de virar ~1 s a 16 kHz mono s16 = ~32000 bytes.
    const int producedSamples = out.size() / 2;
    QVERIFY2(std::abs(producedSamples - 16000) <= 32,
             qPrintable(QStringLiteral("produziu %1 amostras").arg(producedSamples)));
    QCOMPARE(out.size() % 2, 0); // nunca meia amostra
}

void PcmConverterTest::downmixesStereoToMono()
{
    const PcmConverter::InputFormat fmt{16000, 2, SampleType::Int16};
    PcmConverter c(fmt);

    // Dois canais idênticos em meia escala. Somar daria estouro; a média
    // preserva o nível.
    QByteArray in;
    for (int i = 0; i < 1600; ++i) {
        qint16 s = 16000;
        char b[2]; qToLittleEndian(s, b);
        in.append(b, 2); in.append(b, 2);
    }
    const QByteArray out = c.convert(in);
    QVERIFY(!out.isEmpty());
    QVERIFY2(c.lastPeak() < 0.55f && c.lastPeak() > 0.45f,
             qPrintable(QStringLiteral("pico %1 — sinal de soma em vez de média")
                            .arg(c.lastPeak())));
}

void PcmConverterTest::handlesSplitFramesAcrossCalls()
{
    const PcmConverter::InputFormat fmt{48000, 2, SampleType::Int16};
    const QByteArray full = tone(440, 4800, fmt);

    PcmConverter whole(fmt);
    const QByteArray expected = whole.convert(full);

    // O QAudioSource entrega blocos de tamanho arbitrário, que cortam
    // quadros ao meio. Cortar num ponto ímpar é o pior caso.
    PcmConverter chunked(fmt);
    QByteArray got;
    const int cut = 777;
    for (int offset = 0; offset < full.size(); offset += cut) {
        got += chunked.convert(full.constData() + offset,
                               std::min<qsizetype>(cut, full.size() - offset));
    }

    QCOMPARE(got.size(), expected.size());
    QCOMPARE(got, expected);
}

void PcmConverterTest::measuresPeak()
{
    const PcmConverter::InputFormat fmt{48000, 1, SampleType::Int16};
    PcmConverter c(fmt);

    c.convert(tone(440, 4800, fmt, 0.0));
    QVERIFY(c.lastPeak() < 0.01f); // silêncio

    c.convert(tone(440, 4800, fmt, 0.9));
    QVERIFY(c.lastPeak() > 0.8f);
}

void PcmConverterTest::clampsWithoutWrapping()
{
    const PcmConverter::InputFormat fmt{48000, 1, SampleType::Float32};
    PcmConverter c(fmt);

    // Float pode passar de 1,0. Sem clamp o valor daria a volta e um som
    // alto viraria estalo invertido.
    QByteArray in;
    for (int i = 0; i < 4800; ++i) {
        float v = (i % 2 == 0) ? 4.0f : -4.0f;
        in.append(reinterpret_cast<const char *>(&v), 4);
    }
    const QByteArray out = c.convert(in);
    for (int i = 0; i + 1 < out.size(); i += 2) {
        const qint16 s = qFromLittleEndian<qint16>(out.constData() + i);
        QVERIFY(s >= -32768 && s <= 32767);
    }
}

void PcmConverterTest::supportsInt32AndFloat_data()
{
    QTest::addColumn<SampleType>("type");
    // O microfone interno desta máquina entrega s32le.
    QTest::newRow("int16") << SampleType::Int16;
    QTest::newRow("int32") << SampleType::Int32;
    QTest::newRow("float32") << SampleType::Float32;
}

void PcmConverterTest::supportsInt32AndFloat()
{
    QFETCH(SampleType, type);
    const PcmConverter::InputFormat fmt{48000, 2, type};
    PcmConverter c(fmt);

    const QByteArray out = c.convert(tone(440, 48000, fmt, 0.5));
    QVERIFY2(std::abs(out.size() / 2 - 16000) <= 32, "taxa de saída errada");
    // Todos os formatos são normalizados para a mesma escala.
    QVERIFY2(c.lastPeak() > 0.4f && c.lastPeak() < 0.6f,
             qPrintable(QStringLiteral("pico %1").arg(c.lastPeak())));
}

QTEST_MAIN(PcmConverterTest)
#include "PcmConverterTest.moc"
