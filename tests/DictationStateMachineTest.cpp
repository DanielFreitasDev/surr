#include "DictationStateMachine.h"

#include <QSignalSpy>
#include <QTest>

using namespace surr;
using State = DictationStateMachine::State;
using AbortReason = DictationStateMachine::AbortReason;

/**
 * As corridas de apertar/soltar rápido são quase impossíveis de reproduzir à
 * mão numa sessão Wayland — e no Wayland nem dá para simular a tecla. Por
 * isso a máquina de estados não conhece áudio nem rede: aqui os eventos
 * chegam na ordem exata que se quer testar.
 */
class DictationStateMachineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m = new DictationStateMachine(this);
        // Sem debounce e sem mínimo, os testes de fluxo ficam síncronos.
        m->setReleaseDebounceMs(0);
        m->setMinimumHoldMs(0);
    }

    void cleanup() { delete m; m = nullptr; }

    void fullCycle();
    void pressWhileTranscribingIsIgnored();
    void releaseWithoutPressIsIgnored();
    void autoRepeatDoesNotRestart();
    void releaseBeforeCaptureStarted();
    void releaseBeforeCaptureStartedAndTooShort();
    void tooShortNeverReachesTranscription();
    void debounceSwallowsBounce();
    void cancelDuringRecording();
    void shutdownDuringTranscription();
    void emptyTranscriptionAborts();
    void deliveryFailureReturnsToIdle();
    void runIdIncrementsPerCycle();
    void errorLeavesMachineImmediatelyUsable();

private:
    /// Leva a máquina até Transcribing.
    void driveToTranscribing()
    {
        m->onShortcutPressed();
        m->onCaptureStarted();
        m->onShortcutReleased();
        m->onCaptureFinalized(QStringLiteral("/tmp/a.wav"), 1.0);
    }

    DictationStateMachine *m = nullptr;
};

void DictationStateMachineTest::fullCycle()
{
    QSignalSpy started(m, &DictationStateMachine::startCaptureRequested);
    QSignalSpy stopped(m, &DictationStateMachine::stopCaptureRequested);
    QSignalSpy transcribe(m, &DictationStateMachine::transcriptionRequested);
    QSignalSpy deliver(m, &DictationStateMachine::deliveryRequested);
    QSignalSpy completed(m, &DictationStateMachine::cycleCompleted);

    QCOMPARE(m->state(), State::Idle);

    m->onShortcutPressed();
    QCOMPARE(m->state(), State::Recording);
    QCOMPARE(started.count(), 1);

    m->onCaptureStarted();
    QCOMPARE(m->state(), State::Recording);

    m->onShortcutReleased();
    QCOMPARE(m->state(), State::FinalizingAudio);
    QCOMPARE(stopped.count(), 1);

    m->onCaptureFinalized(QStringLiteral("/tmp/a.wav"), 2.0);
    QCOMPARE(m->state(), State::Transcribing);
    QCOMPARE(transcribe.count(), 1);
    QCOMPARE(transcribe.first().at(1).toString(), QStringLiteral("/tmp/a.wav"));

    m->onTranscriptionSucceeded(QStringLiteral("olá mundo"));
    QCOMPARE(m->state(), State::Delivering);
    QCOMPARE(deliver.count(), 1);
    QCOMPARE(deliver.first().at(1).toString(), QStringLiteral("olá mundo"));

    m->onDeliveryFinished(true);
    QCOMPARE(m->state(), State::Idle);
    QCOMPARE(completed.count(), 1);
    // O texto entregue precisa chegar em cycleCompleted, é o que alimenta o
    // histórico e a notificação de sucesso.
    QCOMPARE(completed.first().at(1).toString(), QStringLiteral("olá mundo"));
    QCOMPARE(completed.first().at(2).toBool(), true);
}

void DictationStateMachineTest::pressWhileTranscribingIsIgnored()
{
    driveToTranscribing();
    QCOMPARE(m->state(), State::Transcribing);

    QSignalSpy started(m, &DictationStateMachine::startCaptureRequested);
    const quint64 idBefore = m->runId();

    // Apertar de novo durante a transcrição não pode abrir uma segunda
    // gravação: seriam duas transcrições concorrentes e duas colagens.
    m->onShortcutPressed();
    m->onShortcutReleased();

    QCOMPARE(m->state(), State::Transcribing);
    QCOMPARE(started.count(), 0);
    QCOMPARE(m->runId(), idBefore);
}

void DictationStateMachineTest::releaseWithoutPressIsIgnored()
{
    QSignalSpy stopped(m, &DictationStateMachine::stopCaptureRequested);
    QSignalSpy aborted(m, &DictationStateMachine::cycleAborted);

    m->onShortcutReleased();

    QCOMPARE(m->state(), State::Idle);
    QCOMPARE(stopped.count(), 0);
    QCOMPARE(aborted.count(), 0);
}

void DictationStateMachineTest::autoRepeatDoesNotRestart()
{
    QSignalSpy started(m, &DictationStateMachine::startCaptureRequested);

    m->onShortcutPressed();
    m->onCaptureStarted();
    // Mesmo que um backend passasse a repetição adiante como press, o
    // estado Recording já a torna inócua.
    for (int i = 0; i < 20; ++i) {
        m->onShortcutPressed();
    }

    QCOMPARE(started.count(), 1);
    QCOMPARE(m->state(), State::Recording);

    m->onShortcutReleased();
    QCOMPARE(m->state(), State::FinalizingAudio);
}

void DictationStateMachineTest::releaseBeforeCaptureStarted()
{
    // A captura leva ~140 ms até a primeira amostra. Soltar antes disso é
    // comum, e o pedido de parada não pode se perder.
    QSignalSpy stopped(m, &DictationStateMachine::stopCaptureRequested);

    m->onShortcutPressed();
    m->onShortcutReleased();

    // Ainda gravando: não há o que parar até o dispositivo abrir.
    QCOMPARE(m->state(), State::Recording);
    QCOMPARE(stopped.count(), 0);

    m->onCaptureStarted();

    // Assim que abriu, a parada pendente é executada.
    QCOMPARE(m->state(), State::FinalizingAudio);
    QCOMPARE(stopped.count(), 1);
}

void DictationStateMachineTest::releaseBeforeCaptureStartedAndTooShort()
{
    m->setMinimumHoldMs(10'000); // qualquer toque é curto demais

    QSignalSpy aborted(m, &DictationStateMachine::cycleAborted);
    QSignalSpy abortCapture(m, &DictationStateMachine::abortCaptureRequested);
    QSignalSpy stopped(m, &DictationStateMachine::stopCaptureRequested);

    m->onShortcutPressed();
    m->onShortcutReleased();

    // Curto demais e sem áudio ainda: aborta na hora, sem esperar o
    // dispositivo abrir só para descartar depois.
    QCOMPARE(m->state(), State::Idle);
    QCOMPARE(abortCapture.count(), 1);
    QCOMPARE(stopped.count(), 0);
    QCOMPARE(aborted.count(), 1);
    QCOMPARE(aborted.first().at(1).value<AbortReason>(), AbortReason::TooShort);
}

void DictationStateMachineTest::tooShortNeverReachesTranscription()
{
    m->setMinimumHoldMs(10'000);

    QSignalSpy transcribe(m, &DictationStateMachine::transcriptionRequested);
    QSignalSpy aborted(m, &DictationStateMachine::cycleAborted);

    m->onShortcutPressed();
    m->onCaptureStarted();
    m->onShortcutReleased();

    QCOMPARE(m->state(), State::Idle);
    // Cada chamada à API custa no mínimo 10 s de áudio faturado; um toque
    // acidental não pode virar requisição.
    QCOMPARE(transcribe.count(), 0);
    QCOMPARE(aborted.count(), 1);
    QCOMPARE(aborted.first().at(1).value<AbortReason>(), AbortReason::TooShort);
}

void DictationStateMachineTest::debounceSwallowsBounce()
{
    m->setReleaseDebounceMs(80);

    QSignalSpy stopped(m, &DictationStateMachine::stopCaptureRequested);

    m->onShortcutPressed();
    m->onCaptureStarted();

    m->onShortcutReleased();
    // Ainda dentro da janela: nada aconteceu.
    QCOMPARE(m->state(), State::Recording);
    QCOMPARE(stopped.count(), 0);

    // Um press dentro da janela desfaz o release — foi repique.
    m->onShortcutPressed();
    QTest::qWait(150);
    QCOMPARE(m->state(), State::Recording);
    QCOMPARE(stopped.count(), 0);

    // Um release que não é seguido de press encerra de verdade.
    m->onShortcutReleased();
    QTRY_COMPARE(m->state(), State::FinalizingAudio);
    QCOMPARE(stopped.count(), 1);
}

void DictationStateMachineTest::cancelDuringRecording()
{
    QSignalSpy abortCapture(m, &DictationStateMachine::abortCaptureRequested);
    QSignalSpy aborted(m, &DictationStateMachine::cycleAborted);
    QSignalSpy transcribe(m, &DictationStateMachine::transcriptionRequested);

    m->onShortcutPressed();
    m->onCaptureStarted();
    m->cancel();

    QCOMPARE(m->state(), State::Idle);
    QCOMPARE(abortCapture.count(), 1);
    QCOMPARE(transcribe.count(), 0);
    QCOMPARE(aborted.first().at(1).value<AbortReason>(), AbortReason::Cancelled);
}

void DictationStateMachineTest::shutdownDuringTranscription()
{
    driveToTranscribing();

    QSignalSpy cancelReq(m, &DictationStateMachine::transcriptionCancelRequested);
    QSignalSpy deliver(m, &DictationStateMachine::deliveryRequested);
    QSignalSpy aborted(m, &DictationStateMachine::cycleAborted);

    m->requestShutdown();

    // Fechar o aplicativo no meio de uma transcrição não pode colar texto
    // numa janela qualquer depois.
    QCOMPARE(cancelReq.count(), 1);
    QCOMPARE(deliver.count(), 0);
    QCOMPARE(m->state(), State::Idle);
    QCOMPARE(aborted.first().at(1).value<AbortReason>(), AbortReason::ShuttingDown);

    // E depois de pedir o encerramento, a tecla não inicia mais nada.
    QSignalSpy started(m, &DictationStateMachine::startCaptureRequested);
    m->onShortcutPressed();
    QCOMPARE(started.count(), 0);
}

void DictationStateMachineTest::emptyTranscriptionAborts()
{
    driveToTranscribing();

    QSignalSpy deliver(m, &DictationStateMachine::deliveryRequested);
    QSignalSpy aborted(m, &DictationStateMachine::cycleAborted);

    // Só espaço em branco não é texto: colar isso apagaria uma seleção do
    // usuário sem nada em troca.
    m->onTranscriptionSucceeded(QStringLiteral("   \n  "));

    QCOMPARE(deliver.count(), 0);
    QCOMPARE(m->state(), State::Idle);
    QCOMPARE(aborted.count(), 1);
}

void DictationStateMachineTest::deliveryFailureReturnsToIdle()
{
    driveToTranscribing();
    m->onTranscriptionSucceeded(QStringLiteral("texto"));
    QCOMPARE(m->state(), State::Delivering);

    QSignalSpy aborted(m, &DictationStateMachine::cycleAborted);
    m->onDeliveryFailed(QStringLiteral("sem permissão"));

    QCOMPARE(m->state(), State::Idle);
    QCOMPARE(aborted.first().at(1).value<AbortReason>(), AbortReason::DeliveryError);
}

void DictationStateMachineTest::runIdIncrementsPerCycle()
{
    const quint64 first = [this] {
        m->onShortcutPressed();
        return m->runId();
    }();
    m->onCaptureStarted();
    m->onShortcutReleased();
    m->onCaptureFinalized(QStringLiteral("/tmp/a.wav"), 1.0);
    m->onTranscriptionSucceeded(QStringLiteral("um"));
    m->onDeliveryFinished(true);

    m->onShortcutPressed();
    // O runId é o que permite descartar eventos atrasados de um ciclo velho.
    QCOMPARE(m->runId(), first + 1);
}

void DictationStateMachineTest::errorLeavesMachineImmediatelyUsable()
{
    driveToTranscribing();
    m->onTranscriptionFailed(QStringLiteral("rede fora"));
    QCOMPARE(m->state(), State::Idle);

    // Erro recuperável de verdade: a tecla responde na sequência, sem
    // precisar de confirmação nem de reinício.
    QSignalSpy started(m, &DictationStateMachine::startCaptureRequested);
    m->onShortcutPressed();
    QCOMPARE(started.count(), 1);
    QCOMPARE(m->state(), State::Recording);
}

QTEST_MAIN(DictationStateMachineTest)
#include "DictationStateMachineTest.moc"
