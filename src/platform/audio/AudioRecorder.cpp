#include "AudioRecorder.h"

#include "AudioEngine.h"
#include "SurrDebug.h"

#include <QDir>
#include <QMediaDevices>
#include <QStandardPaths>
#include <QThread>

namespace surr {

struct AudioRecorder::Private {
    QThread *thread = nullptr;
    AudioEngine *engine = nullptr;
    QString deviceId;
    QString currentPath;
};

AudioRecorder::AudioRecorder(QObject *parent) : QObject(parent), d(new Private)
{
    d->thread = new QThread;
    d->thread->setObjectName(QStringLiteral("surr-audio"));

    // Sem pai: um QObject com pai não pode ser movido de thread.
    d->engine = new AudioEngine;
    d->engine->moveToThread(d->thread);
    connect(d->thread, &QThread::finished, d->engine, &QObject::deleteLater);

    // Conexões enfileiradas por serem entre threads — é o que mantém o
    // QAudioSource confinado à thread de áudio.
    connect(d->engine, &AudioEngine::captureStarted, this, &AudioRecorder::captureStarted);
    connect(d->engine, &AudioEngine::captureFailed, this, &AudioRecorder::captureFailed);
    connect(d->engine, &AudioEngine::captureFinished, this, &AudioRecorder::captureFinished);
    connect(d->engine, &AudioEngine::levelChanged, this, &AudioRecorder::levelChanged);

    d->thread->start();
}

AudioRecorder::~AudioRecorder()
{
    d->thread->quit();
    // Espera limitada: travar o logout é pior do que perder uma gravação.
    if (!d->thread->wait(2000)) {
        qCWarning(SURR_AUDIO) << "a thread de áudio não encerrou a tempo";
        d->thread->terminate();
        d->thread->wait(500);
    }
    delete d->thread;
    delete d;
}

void AudioRecorder::setDeviceId(const QString &id) { d->deviceId = id; }
QString AudioRecorder::deviceId() const { return d->deviceId; }
QString AudioRecorder::currentPath() const { return d->currentPath; }

QList<QPair<QString, QString>> AudioRecorder::availableInputs()
{
    QList<QPair<QString, QString>> result;
    const auto inputs = QMediaDevices::audioInputs();
    result.reserve(inputs.size());
    for (const QAudioDevice &device : inputs) {
        result.append({QString::fromUtf8(device.id()), device.description()});
    }
    return result;
}

void AudioRecorder::start(quint64 runId)
{
    // Um arquivo por ciclo, em diretório de runtime privado do usuário.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + QStringLiteral("/surr");
    QDir().mkpath(dir);
    d->currentPath = QStringLiteral("%1/ditado-%2.wav").arg(dir).arg(runId);

    QMetaObject::invokeMethod(d->engine, "start", Qt::QueuedConnection,
                              Q_ARG(quint64, runId), Q_ARG(QString, d->deviceId),
                              Q_ARG(QString, d->currentPath));
}

void AudioRecorder::stop(quint64 runId)
{
    QMetaObject::invokeMethod(d->engine, "stop", Qt::QueuedConnection, Q_ARG(quint64, runId));
}

void AudioRecorder::abort(quint64 runId)
{
    QMetaObject::invokeMethod(d->engine, "abort", Qt::QueuedConnection, Q_ARG(quint64, runId));
}

} // namespace surr
