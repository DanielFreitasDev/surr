#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QTimer;

namespace surr {

/**
 * Máquina de estados do ciclo de ditado.
 *
 * Não conhece áudio, rede nem UI: recebe eventos e emite ordens. Isso a torna
 * testável sem sessão gráfica, que é onde as corridas de apertar/soltar rápido
 * são difíceis de reproduzir à mão.
 *
 *   Idle → Recording → FinalizingAudio → Transcribing → Delivering → Idle
 *
 * Não há estado de erro persistente: toda falha volta a `Idle` e é relatada
 * por `cycleAborted()`. É o que torna o erro recuperável de fato — logo após
 * uma falha a tecla já responde de novo, sem precisar de confirmação.
 *
 * Invariantes que o desenho garante:
 *  - Nunca há duas transcrições em voo: `Activated` fora de `Idle` é ignorado.
 *  - Soltar a tecla encerra no máximo uma gravação, graças ao `runId`.
 *  - Soltar antes de o microfone abrir não perde o evento (`pendingStop`).
 *  - O auto-repeat do teclado não entra aqui: o chamador não escuta o sinal
 *    de repetição do KWin, e um `pressed()` repetido fora de `Idle` é inócuo.
 */
class DictationStateMachine : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Idle,
        Recording,
        FinalizingAudio,
        Transcribing,
        Delivering,
    };
    Q_ENUM(State)

    /// Motivo pelo qual um ciclo terminou sem entregar texto.
    enum class AbortReason {
        TooShort,       ///< Tecla segurada por menos que o mínimo.
        NoAudio,        ///< Tecla segurada, mas nenhuma amostra chegou.
        AudioError,
        TranscriptionError,
        DeliveryError,
        Cancelled,
        ShuttingDown,
    };
    Q_ENUM(AbortReason)

    explicit DictationStateMachine(QObject *parent = nullptr);
    ~DictationStateMachine() override;

    State state() const;
    bool isBusy() const { return state() != State::Idle; }

    /// Tempo mínimo de tecla segurada para valer um ditado. Padrão 450 ms.
    void setMinimumHoldMs(int ms);
    int minimumHoldMs() const;

    /**
     * Janela em que um `pressed()` cancela um `released()` recém-chegado.
     * Padrão 60 ms. Os testes usam 0 para rodar sem esperar.
     */
    void setReleaseDebounceMs(int ms);
    int releaseDebounceMs() const;

    /// Identificador do ciclo atual; incrementa a cada gravação iniciada.
    quint64 runId() const;

public Q_SLOTS:
    // --- Eventos vindos do atalho global ---
    void onShortcutPressed();
    void onShortcutReleased();

    // --- Eventos vindos do gravador ---
    /// O dispositivo abriu e as amostras começaram a chegar.
    void onCaptureStarted();
    void onCaptureFailed(const QString &message);
    /// A gravação foi fechada; @p path é o WAV pronto.
    void onCaptureFinalized(const QString &path, double audioSeconds);

    // --- Eventos vindos da transcrição ---
    void onTranscriptionSucceeded(const QString &text);
    void onTranscriptionFailed(const QString &message);

    // --- Eventos vindos da entrega ---
    void onDeliveryFinished(bool pasted);
    void onDeliveryFailed(const QString &message);

    // --- Eventos externos ---
    void cancel();          ///< Esc, ou o usuário pediu para abortar.
    void requestShutdown(); ///< O aplicativo está encerrando.

Q_SIGNALS:
    /// Abra o microfone e comece a capturar.
    void startCaptureRequested(quint64 runId);
    /// Feche a captura e finalize o WAV.
    void stopCaptureRequested(quint64 runId);
    /// Descarte a captura sem transcrever (curta demais, cancelada).
    void abortCaptureRequested(quint64 runId);
    /// Envie @p wavPath ao provider selecionado.
    void transcriptionRequested(quint64 runId, const QString &wavPath);
    /// Cancele a requisição em voo.
    void transcriptionCancelRequested(quint64 runId);
    /// Copie e cole @p text.
    void deliveryRequested(quint64 runId, const QString &text);

    void stateChanged(surr::DictationStateMachine::State state);
    /// Ciclo concluído com sucesso.
    void cycleCompleted(quint64 runId, const QString &text, bool pasted);
    /// Ciclo encerrado sem entregar texto.
    void cycleAborted(quint64 runId, surr::DictationStateMachine::AbortReason reason,
                      const QString &message);

private:
    void setState(State next);
    void beginStop();
    void finishAbort(AbortReason reason, const QString &message);

    struct Private;
    Private *d;
};

} // namespace surr
