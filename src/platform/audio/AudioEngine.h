#pragma once

#include "PcmConverter.h"
#include "WavWriter.h"

#include <QAudioDevice>
#include <QObject>
#include <memory>

class QAudioSource;
class QIODevice;
class QTimer;

namespace surr {

/**
 * Captura do microfone. Vive inteiramente na thread de áudio.
 *
 * O `QAudioSource` não é seguro entre threads: é construído, iniciado, lido,
 * parado e destruído só aqui. Quem chama de fora usa `AudioRecorder`, que
 * enfileira as chamadas.
 */
class AudioEngine : public QObject
{
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

public Q_SLOTS:
    /// @p deviceId vazio usa o microfone padrão do sistema.
    void start(quint64 runId, const QString &deviceId, const QString &wavPath);
    void stop(quint64 runId);
    /// Encerra e apaga o arquivo — usado quando o ditado é descartado.
    void abort(quint64 runId);

Q_SIGNALS:
    /// As primeiras amostras chegaram. Só aqui a captura está de fato viva:
    /// o dispositivo leva ~140 ms até o primeiro byte, e dizer "gravando"
    /// antes disso faria o usuário falar em cima do silêncio.
    void captureStarted(quint64 runId);
    void captureFailed(quint64 runId, const QString &message);
    void captureFinished(quint64 runId, const QString &path, double seconds);
    /// Pico do intervalo, 0..1 já em escala perceptual.
    void levelChanged(float level);

private Q_SLOTS:
    void onReadyRead();
    void onStateChanged();
    void emitLevel();

private:
    void teardown();
    void markLive();
    void fail(const QString &message);

    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace surr
