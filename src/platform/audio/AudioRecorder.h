#pragma once

#include <QObject>
#include <QString>

namespace surr {

class AudioEngine;

/**
 * Fachada do gravador na thread principal.
 *
 * Dona da thread de áudio e do `AudioEngine`. Todas as chamadas entram
 * enfileiradas e todos os resultados voltam por sinal — a UI nunca toca no
 * `QAudioSource`, e finalizar o WAV não bloqueia o desenho da interface.
 */
class AudioRecorder : public QObject
{
    Q_OBJECT
public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    /// Identificador do dispositivo (`QAudioDevice::id()`); vazio = padrão.
    void setDeviceId(const QString &id);
    QString deviceId() const;

    /// Lista os microfones disponíveis como pares (id, nome legível).
    static QList<QPair<QString, QString>> availableInputs();

    void start(quint64 runId);
    void stop(quint64 runId);
    void abort(quint64 runId);

    /// Caminho do WAV do ciclo corrente (temporário, 0600).
    QString currentPath() const;

Q_SIGNALS:
    void captureStarted(quint64 runId);
    void captureFailed(quint64 runId, const QString &message);
    void captureFinished(quint64 runId, const QString &path, double seconds);
    void levelChanged(float level);

private:
    struct Private;
    Private *d;
};

} // namespace surr
