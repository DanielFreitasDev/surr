#include "DictationStateMachine.h"

#include <QTimer>

namespace surr {

namespace {
constexpr int kDefaultMinimumHoldMs = 450;
constexpr int kDefaultReleaseDebounceMs = 60;
} // namespace

struct DictationStateMachine::Private {
    State state = State::Idle;
    quint64 runId = 0;

    int minimumHoldMs = kDefaultMinimumHoldMs;
    int releaseDebounceMs = kDefaultReleaseDebounceMs;

    /// Cronômetro da tecla. A duração mínima é medida aqui, e não no áudio:
    /// a captura leva ~140 ms até a primeira amostra, e cobrar isso do
    /// usuário faria 0,55 s de tecla virarem 0,4 s de áudio e serem descartados.
    QElapsedTimer holdTimer;

    /// A tecla foi solta antes de o microfone abrir.
    bool pendingStop = false;
    /// A captura confirmou que amostras chegaram.
    bool captureLive = false;
    /// Encerrando: não iniciar nada novo.
    bool shuttingDown = false;
    /// Texto entregue no ciclo corrente, para relatar em `cycleCompleted`.
    QString deliveredText;

    QTimer *releaseTimer = nullptr;
};

DictationStateMachine::DictationStateMachine(QObject *parent)
    : QObject(parent), d(new Private)
{
    d->releaseTimer = new QTimer(this);
    d->releaseTimer->setSingleShot(true);
    d->releaseTimer->setInterval(d->releaseDebounceMs);
    connect(d->releaseTimer, &QTimer::timeout, this, [this] { beginStop(); });
}

DictationStateMachine::~DictationStateMachine() { delete d; }

DictationStateMachine::State DictationStateMachine::state() const { return d->state; }
quint64 DictationStateMachine::runId() const { return d->runId; }

void DictationStateMachine::setMinimumHoldMs(int ms) { d->minimumHoldMs = qMax(0, ms); }
int DictationStateMachine::minimumHoldMs() const { return d->minimumHoldMs; }

void DictationStateMachine::setReleaseDebounceMs(int ms)
{
    d->releaseDebounceMs = qMax(0, ms);
    d->releaseTimer->setInterval(d->releaseDebounceMs);
}
int DictationStateMachine::releaseDebounceMs() const { return d->releaseDebounceMs; }

void DictationStateMachine::setState(State next)
{
    if (d->state == next) {
        return;
    }
    d->state = next;
    Q_EMIT stateChanged(next);
}

void DictationStateMachine::onShortcutPressed()
{
    // Um press que chega dentro da janela de debounce desfaz o release:
    // foi um repique, não um novo ditado.
    if (d->releaseTimer->isActive()) {
        d->releaseTimer->stop();
        return;
    }

    if (d->shuttingDown) {
        return;
    }

    // Fora de Idle o press é ignorado — nunca enfileirado. Enfileirar
    // permitiria duas transcrições concorrentes, que é exatamente o que
    // apertar a tecla de novo durante a transcrição não deve provocar.
    if (d->state != State::Idle) {
        return;
    }

    ++d->runId;
    d->pendingStop = false;
    d->captureLive = false;
    d->holdTimer.start();

    setState(State::Recording);
    Q_EMIT startCaptureRequested(d->runId);
}

void DictationStateMachine::onShortcutReleased()
{
    if (d->state != State::Recording) {
        // Release sem gravação correspondente (repique, ou o ciclo já
        // terminou por outro motivo). Ignorar.
        return;
    }

    if (d->releaseDebounceMs <= 0) {
        beginStop();
        return;
    }
    d->releaseTimer->start();
}

void DictationStateMachine::beginStop()
{
    if (d->state != State::Recording) {
        return;
    }

    const qint64 heldMs = d->holdTimer.isValid() ? d->holdTimer.elapsed() : 0;

    if (!d->captureLive) {
        // A tecla foi solta antes de o dispositivo abrir. Não dá para parar
        // uma captura que ainda não começou; marca-se a intenção e o
        // `onCaptureStarted()` a executa assim que houver o que parar.
        d->pendingStop = true;
        if (heldMs < d->minimumHoldMs) {
            // Curto demais de qualquer forma: aborta sem esperar o áudio.
            Q_EMIT abortCaptureRequested(d->runId);
            finishAbort(AbortReason::TooShort,
                        QStringLiteral("Gravação curta demais — segure a tecla enquanto fala."));
        }
        return;
    }

    if (heldMs < d->minimumHoldMs) {
        Q_EMIT abortCaptureRequested(d->runId);
        finishAbort(AbortReason::TooShort,
                    QStringLiteral("Gravação curta demais — segure a tecla enquanto fala."));
        return;
    }

    setState(State::FinalizingAudio);
    Q_EMIT stopCaptureRequested(d->runId);
}

void DictationStateMachine::onCaptureStarted()
{
    if (d->state != State::Recording) {
        return;
    }
    d->captureLive = true;

    if (d->pendingStop) {
        d->pendingStop = false;
        beginStop();
    }
}

void DictationStateMachine::onCaptureFailed(const QString &message)
{
    if (d->state != State::Recording && d->state != State::FinalizingAudio) {
        return;
    }
    Q_EMIT abortCaptureRequested(d->runId);
    finishAbort(AbortReason::AudioError, message);
}

void DictationStateMachine::onCaptureFinalized(const QString &path, double audioSeconds)
{
    if (d->state != State::FinalizingAudio) {
        return;
    }

    if (path.isEmpty() || audioSeconds <= 0.0) {
        finishAbort(AbortReason::NoAudio,
                    QStringLiteral("Nenhum áudio foi capturado. Verifique o microfone."));
        return;
    }

    if (d->shuttingDown) {
        finishAbort(AbortReason::ShuttingDown, QString());
        return;
    }

    setState(State::Transcribing);
    Q_EMIT transcriptionRequested(d->runId, path);
}

void DictationStateMachine::onTranscriptionSucceeded(const QString &text)
{
    if (d->state != State::Transcribing) {
        return;
    }

    if (text.trimmed().isEmpty()) {
        finishAbort(AbortReason::TranscriptionError,
                    QStringLiteral("Nada foi reconhecido no áudio."));
        return;
    }

    d->deliveredText = text;
    setState(State::Delivering);
    Q_EMIT deliveryRequested(d->runId, text);
}

void DictationStateMachine::onTranscriptionFailed(const QString &message)
{
    if (d->state != State::Transcribing) {
        return;
    }
    finishAbort(AbortReason::TranscriptionError, message);
}

void DictationStateMachine::onDeliveryFinished(bool pasted)
{
    if (d->state != State::Delivering) {
        return;
    }
    const quint64 id = d->runId;
    const QString text = d->deliveredText;
    d->deliveredText.clear();
    setState(State::Idle);
    Q_EMIT cycleCompleted(id, text, pasted);
}

void DictationStateMachine::onDeliveryFailed(const QString &message)
{
    if (d->state != State::Delivering) {
        return;
    }
    finishAbort(AbortReason::DeliveryError, message);
}

void DictationStateMachine::cancel()
{
    switch (d->state) {
    case State::Idle:
        return;
    case State::Recording:
    case State::FinalizingAudio:
        d->releaseTimer->stop();
        Q_EMIT abortCaptureRequested(d->runId);
        break;
    case State::Transcribing:
        Q_EMIT transcriptionCancelRequested(d->runId);
        break;
    case State::Delivering:
        // A colagem já está em voo e é curta; deixá-la terminar é mais
        // previsível do que interrompê-la no meio de um Ctrl+V.
        return;
    }
    finishAbort(AbortReason::Cancelled, QString());
}

void DictationStateMachine::requestShutdown()
{
    d->shuttingDown = true;
    d->releaseTimer->stop();

    switch (d->state) {
    case State::Idle:
        return;
    case State::Recording:
    case State::FinalizingAudio:
        Q_EMIT abortCaptureRequested(d->runId);
        break;
    case State::Transcribing:
        Q_EMIT transcriptionCancelRequested(d->runId);
        break;
    case State::Delivering:
        break;
    }
    finishAbort(AbortReason::ShuttingDown, QString());
}

void DictationStateMachine::finishAbort(AbortReason reason, const QString &message)
{
    const quint64 id = d->runId;
    d->pendingStop = false;
    d->captureLive = false;
    d->deliveredText.clear();
    setState(State::Idle);
    Q_EMIT cycleAborted(id, reason, message);
}

} // namespace surr
