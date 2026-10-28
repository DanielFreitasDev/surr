#include "AudioEngine.h"

#include "SurrDebug.h"

#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace surr {

namespace {
/// Cadência do medidor. 20 Hz é bastante para a animação parecer viva sem
/// inundar a thread da UI com sinais; o QML interpola entre as amostras.
constexpr int kLevelIntervalMs = 50;

/// Piso da escala perceptual. Em escala linear a barra quase não se mexe
/// com voz normal — medido no aplicativo anterior.
constexpr double kFloorDb = -55.0;

/// Teto da espera por áudio de verdade (ver onReadyRead). O fone Bluetooth
/// medido entregou zeros por ~1,1 s; sem o teto, um microfone mudo de
/// verdade deixaria o ciclo em "Preparando" para sempre.
constexpr int kLiveFallbackMs = 2500;

PcmConverter::SampleType toSampleType(QAudioFormat::SampleFormat f)
{
    switch (f) {
    case QAudioFormat::Int16: return PcmConverter::SampleType::Int16;
    case QAudioFormat::Int32: return PcmConverter::SampleType::Int32;
    case QAudioFormat::Float: return PcmConverter::SampleType::Float32;
    default: return PcmConverter::SampleType::Int16;
    }
}

float perceptual(float peak)
{
    if (peak <= 0.0f) {
        return 0.0f;
    }
    const double db = 20.0 * std::log10(static_cast<double>(peak));
    const double norm = (db - kFloorDb) / -kFloorDb;
    return static_cast<float>(std::clamp(norm, 0.0, 1.0));
}
} // namespace

struct AudioEngine::Private {
    QAudioSource *source = nullptr;
    QIODevice *stream = nullptr;
    QTimer *levelTimer = nullptr;
    QTimer *liveFallback = nullptr;

    std::unique_ptr<PcmConverter> converter;
    WavWriter writer;

    quint64 runId = 0;
    bool live = false;
    float intervalPeak = 0.0f;
};

AudioEngine::AudioEngine(QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    d->levelTimer = new QTimer(this);
    d->levelTimer->setInterval(kLevelIntervalMs);
    connect(d->levelTimer, &QTimer::timeout, this, &AudioEngine::emitLevel);

    d->liveFallback = new QTimer(this);
    d->liveFallback->setSingleShot(true);
    d->liveFallback->setInterval(kLiveFallbackMs);
    connect(d->liveFallback, &QTimer::timeout, this, [this] {
        if (d->source && !d->live) {
            qCInfo(SURR_AUDIO) << "só silêncio digital após" << kLiveFallbackMs
                               << "ms; seguindo mesmo assim";
            markLive();
        }
    });
}

void AudioEngine::markLive()
{
    d->live = true;
    d->liveFallback->stop();
    Q_EMIT captureStarted(d->runId);
}

AudioEngine::~AudioEngine() { teardown(); }

void AudioEngine::fail(const QString &message)
{
    const quint64 id = d->runId;
    teardown();
    d->writer.discard();
    Q_EMIT captureFailed(id, message);
}

void AudioEngine::start(quint64 runId, const QString &deviceId, const QString &wavPath)
{
    if (d->source) {
        teardown();
    }
    d->runId = runId;
    d->live = false;
    d->intervalPeak = 0.0f;

    QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (!deviceId.isEmpty()) {
        const auto inputs = QMediaDevices::audioInputs();
        const auto it = std::find_if(inputs.begin(), inputs.end(), [&](const QAudioDevice &d) {
            return QString::fromUtf8(d.id()) == deviceId;
        });
        // Microfone escolhido desapareceu (desconectado): cair no padrão é
        // melhor do que recusar a gravar.
        if (it != inputs.end()) {
            device = *it;
        } else {
            qCWarning(SURR_AUDIO) << "microfone escolhido não encontrado; usando o padrão";
        }
    }

    if (device.isNull()) {
        fail(QStringLiteral("Nenhum microfone disponível."));
        return;
    }

    // Alvo do STT. Se o dispositivo aceitar, o PipeWire converte por nós.
    QAudioFormat format;
    format.setSampleRate(PcmConverter::kTargetRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    if (!device.isFormatSupported(format)) {
        // Nenhum microfone desta máquina oferece 16 kHz mono nativamente.
        // Captura no formato preferido e converte em processo.
        format = device.preferredFormat();
        qCInfo(SURR_AUDIO) << "formato alvo recusado; capturando em"
                           << format.sampleRate() << "Hz" << format.channelCount() << "canais";
    }

    PcmConverter::InputFormat inFmt;
    inFmt.sampleRate = format.sampleRate();
    inFmt.channels = format.channelCount();
    inFmt.type = toSampleType(format.sampleFormat());
    d->converter = std::make_unique<PcmConverter>(inFmt);

    if (!d->writer.open(wavPath, {PcmConverter::kTargetRate, 1, 16})) {
        fail(QStringLiteral("Não foi possível criar o arquivo de áudio: %1")
                 .arg(d->writer.errorString()));
        return;
    }

    d->source = new QAudioSource(device, format, this);
    connect(d->source, &QAudioSource::stateChanged, this, &AudioEngine::onStateChanged);

    d->stream = d->source->start();
    if (!d->stream) {
        fail(QStringLiteral("Não foi possível abrir o microfone."));
        return;
    }
    connect(d->stream, &QIODevice::readyRead, this, &AudioEngine::onReadyRead);
    d->levelTimer->start();
    d->liveFallback->start();

    qCDebug(SURR_AUDIO) << "captura iniciada, ciclo" << runId;
}

void AudioEngine::onStateChanged()
{
    if (!d->source) {
        return;
    }
    const QAudio::Error error = d->source->error();
    if (error != QAudio::NoError && error != QAudio::UnderrunError) {
        // O microfone sumiu no meio da frase (USB desconectado, por exemplo).
        qCWarning(SURR_AUDIO) << "erro do dispositivo:" << error;
        fail(QStringLiteral("O microfone ficou indisponível durante a gravação."));
    }
}

void AudioEngine::onReadyRead()
{
    if (!d->stream || !d->converter) {
        return;
    }
    const QByteArray raw = d->stream->readAll();
    if (raw.isEmpty()) {
        return;
    }

    const QByteArray pcm = d->converter->convert(raw);
    d->intervalPeak = std::max(d->intervalPeak, d->converter->lastPeak());

    // "Vivo" é a primeira amostra diferente de zero, não o primeiro buffer.
    // O fone Bluetooth começa em A2DP, sem microfone, e o WirePlumber só o
    // troca para HFP depois que a gravação abre: medido com um Galaxy Buds2
    // Pro, ~1,1 s de zeros exatos antes da primeira amostra do microfone.
    // Marcando no primeiro buffer, a cápsula dizia "gravando" e a fala desse
    // trecho se perdia. Microfone com fio nunca entrega zero exato — o ruído
    // de fundo basta —, então para eles nada muda.
    if (!d->live && d->converter->lastPeak() > 0.0f) {
        markLive();
    }

    if (!pcm.isEmpty() && !d->writer.write(pcm)) {
        fail(QStringLiteral("Falha ao gravar o áudio em disco."));
    }
}

void AudioEngine::emitLevel()
{
    // Pico do intervalo, zerado a cada leitura: assim um transiente curto
    // nunca some entre duas amostragens do medidor.
    const float level = perceptual(d->intervalPeak);
    d->intervalPeak = 0.0f;
    Q_EMIT levelChanged(level);
}

void AudioEngine::stop(quint64 runId)
{
    if (runId != d->runId || !d->source) {
        return;
    }

    // Drena o que ainda estiver no buffer antes de fechar; sem isso o final
    // da última palavra se perde.
    onReadyRead();
    teardown();

    const double seconds = d->writer.durationSeconds();
    const QString path = d->writer.path();
    if (!d->writer.finalize()) {
        Q_EMIT captureFailed(runId, QStringLiteral("Não foi possível finalizar o áudio."));
        return;
    }

    qCDebug(SURR_AUDIO) << "captura encerrada," << seconds << "s";
    Q_EMIT captureFinished(runId, path, seconds);
}

void AudioEngine::abort(quint64 runId)
{
    if (runId != d->runId) {
        return;
    }
    teardown();
    d->writer.discard(); // nada de deixar áudio do usuário para trás
}

void AudioEngine::teardown()
{
    d->levelTimer->stop();
    d->liveFallback->stop();
    if (d->stream) {
        disconnect(d->stream, nullptr, this, nullptr);
        d->stream = nullptr;
    }
    if (d->source) {
        d->source->stop();
        d->source->deleteLater();
        d->source = nullptr;
    }
    d->live = false;
    Q_EMIT levelChanged(0.0f);
}

} // namespace surr
