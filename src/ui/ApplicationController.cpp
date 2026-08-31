#include "ApplicationController.h"

#include "AudioRecorder.h"
#include "CepifyProvider.h"
#include "ClipboardManager.h"
#include "DeliveryCoordinator.h"
#include "DictationStateMachine.h"
#include "EisInjector.h"
#include "GlobalShortcutManager.h"
#include "GroqProvider.h"
#include "OverlayController.h"
#include "ProxyResolver.h"
#include "RemoteDesktopSession.h"
#include "SecretStore.h"
#include "Settings.h"
#include "SurrDebug.h"
#include "TrayIcon.h"
#include "surr_config.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>

namespace surr {

namespace {
/// Atraso antes de aquecer a sessão do portal. O diálogo de autorização
/// rouba foco; pedir isso no meio de um ditado faria o Ctrl+V chegar na
/// janela errada. Pedir logo depois da partida deixa o caminho livre.
constexpr int kPortalWarmupMs = 1500;
/// Teto de reconexões do EIS antes de desistir e ficar só na cópia.
constexpr int kMaxEisRetries = 3;
} // namespace

struct ApplicationController::Private {
    Settings *settings = nullptr;
    SecretStore *secrets = nullptr;
    QNetworkAccessManager *nam = nullptr;

    DictationStateMachine *machine = nullptr;
    GlobalShortcutManager *shortcuts = nullptr;
    AudioRecorder *recorder = nullptr;
    OverlayController *overlay = nullptr;
    TrayIcon *tray = nullptr;

    RemoteDesktopSession *session = nullptr;
    ClipboardManager *clipboard = nullptr;
    EisInjector *injector = nullptr;
    DeliveryCoordinator *delivery = nullptr;

    GroqProvider *groq = nullptr;
    CepifyProvider *cepify = nullptr;
    ITranscriptionProvider *active = nullptr;

    bool shuttingDown = false;
    int eisRetries = 0;
};

ApplicationController::ApplicationController(QObject *parent) : QObject(parent), d(new Private) {}

ApplicationController::~ApplicationController() { delete d; }

bool ApplicationController::start()
{
    // O proxy precisa entrar antes de qualquer requisição: numa rede que o
    // exige, o autostart não herda as variáveis do shell.
    ProxyResolver::applyToApplication();

    d->settings = new Settings(this);
    d->secrets = new SecretStore(this);
    d->nam = new QNetworkAccessManager(this);

    d->machine = new DictationStateMachine(this);
    d->machine->setMinimumHoldMs(d->settings->minimumHoldMs());
    // Sem debounce: o auto-repeat não chega pelo portal (medido), e 60 ms de
    // atraso no encerramento não compram nada aqui.
    d->machine->setReleaseDebounceMs(0);

    d->recorder = new AudioRecorder(this);
    d->recorder->setDeviceId(d->settings->inputDeviceId());

    d->overlay = new OverlayController(this);
    if (!d->overlay->initialize()) {
        qCWarning(SURR_UI) << "a cápsula não pôde ser criada; seguindo sem ela";
    }

    d->tray = new TrayIcon(this);
    d->tray->setAutostartEnabled(d->settings->autostart());

    d->groq = new GroqProvider(d->nam, this);
    d->cepify = new CepifyProvider(d->nam, this);
    d->cepify->setBaseUrl(d->settings->cepifyBaseUrl());
    d->cepify->setHotwords(d->settings->hotwords());

    d->session = new RemoteDesktopSession(this);
    d->clipboard = new ClipboardManager(this);
    d->injector = new EisInjector(this);
    d->delivery = new DeliveryCoordinator(d->clipboard, d->injector, this);
    d->delivery->setAutoPaste(d->settings->autoPaste());
    d->delivery->setTerminalMode(d->settings->terminalPasteMode());
    d->delivery->setPasteDelayMs(d->settings->pasteDelayMs());

    // ---- Credenciais -------------------------------------------------
    connect(d->secrets, &SecretStore::readyChanged, this, [this](bool) {
        // Importa a chave que o aplicativo anterior deixou em texto puro.
        if (!d->secrets->has(QLatin1String(SecretStore::kGroqKey))
            && !SecretStore::groqKeyComesFromEnvironment()) {
            const QString legacy = SecretStore::findLegacyGroqKey();
            if (!legacy.isEmpty()
                && d->secrets->write(QLatin1String(SecretStore::kGroqKey), legacy)) {
                qCInfo(SURR_APP) << "chave da Groq importada para o KWallet";
            }
        }
        d->groq->setApiKey(d->secrets->groqApiKey());
        d->cepify->setApiKey(d->secrets->read(QLatin1String(SecretStore::kCepifyKey)));

        const bool useCepify = d->settings->provider() == QLatin1String(CepifyProvider::kId);
        d->active = useCepify ? static_cast<ITranscriptionProvider *>(d->cepify)
                              : static_cast<ITranscriptionProvider *>(d->groq);
        d->tray->setProviderName(d->active->displayName());
        d->tray->setStatus(d->active->isConfigured() ? TrayIcon::Status::Idle
                                                     : TrayIcon::Status::Unconfigured);

        d->session->setRestoreToken(d->secrets->read(QLatin1String(SecretStore::kRestoreToken)));
        // Só aquece a sessão se já houver autorização guardada; sem token,
        // abrir o diálogo sozinho na partida roubaria foco sem o usuário pedir.
        QTimer::singleShot(kPortalWarmupMs, this, [this] {
            if (!d->secrets->read(QLatin1String(SecretStore::kRestoreToken)).isEmpty()) {
                d->session->open(QString(), /*interactive=*/false);
            } else {
                d->tray->setPasteAuthorized(false);
            }
        });
    });
    d->secrets->open();

    // ---- Atalho global ------------------------------------------------
    d->shortcuts = new GlobalShortcutManager(this);
    d->shortcuts->setPreferredTrigger(d->settings->preferredTrigger());
    connect(d->shortcuts, &GlobalShortcutManager::pressed, d->machine,
            &DictationStateMachine::onShortcutPressed);
    connect(d->shortcuts, &GlobalShortcutManager::released, d->machine,
            &DictationStateMachine::onShortcutReleased);
    connect(d->shortcuts, &GlobalShortcutManager::triggerDescriptionChanged, this,
            [this](const QString &t) { d->tray->setTriggerDescription(t); });
    connect(d->shortcuts, &GlobalShortcutManager::unavailable, this, [this](const QString &why) {
        qCWarning(SURR_SHORTCUT) << why;
        d->tray->setStatus(TrayIcon::Status::Error, QStringLiteral("Atalho indisponível"));
        d->tray->notify(QStringLiteral("Atalho global indisponível"), why, true);
    });
    d->shortcuts->bind();

    // ---- Sessão do portal / entrega ------------------------------------
    connect(d->session, &RemoteDesktopSession::restoreTokenChanged, this,
            [this](const QString &token) {
                // Uso único: gravar o novo é o que mantém a permissão viva.
                d->secrets->write(QLatin1String(SecretStore::kRestoreToken), token);
            });
    connect(d->session, &RemoteDesktopSession::ready, this, [this](int eisFd) {
        d->clipboard->setSession(d->session->sessionHandle());
        // Sobreviveu 60 s: considera-se estável e o contador volta a zero.
        QTimer::singleShot(60'000, this, [this] { d->eisRetries = 0; });
        if (!d->injector->attach(eisFd)) {
            qCWarning(SURR_INJECT) << "não foi possível anexar o EIS";
        }
        d->tray->setPasteAuthorized(true);
    });
    connect(d->session, &RemoteDesktopSession::failed, this,
            [this](const QString &message, bool) {
                qCInfo(SURR_PORTAL) << "sessão de colagem indisponível:" << message;
                d->tray->setPasteAuthorized(false);
            });
    connect(d->session, &RemoteDesktopSession::closed, this, [this] {
        d->clipboard->clearSession();
        d->injector->detach();
        d->tray->setPasteAuthorized(false);
    });
    connect(d->injector, &EisInjector::disconnected, this, [this] {
        // O descritor não é reaproveitável: a sessão inteira precisa ser
        // refeita. Com espera crescente e teto de tentativas — sem isso,
        // uma recusa persistente do compositor vira laço de reconexão a
        // cada segundo, que foi o que aconteceu na primeira versão.
        d->clipboard->clearSession();
        if (d->eisRetries >= kMaxEisRetries) {
            qCWarning(SURR_INJECT)
                << "EIS caiu" << d->eisRetries << "vezes; desistindo da colagem automática";
            d->tray->setPasteAuthorized(false);
            d->tray->notify(QStringLiteral("Surr"),
                            QStringLiteral("A colagem automática ficou indisponível. "
                                           "O texto continua indo para a área de transferência."),
                            true);
            return;
        }
        const int delayMs = 2000 * (1 << d->eisRetries); // 2 s, 4 s, 8 s
        ++d->eisRetries;
        QTimer::singleShot(delayMs, this, [this] {
            if (d->shuttingDown || d->machine->state() != DictationStateMachine::State::Idle) {
                return;
            }
            d->session->close();
            d->session->open(QString(), false);
        });
    });

    // O portal pode reiniciar; a sessão morre junto e precisa ser refeita.
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.freedesktop.portal.Desktop"),
                                            QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
                if (newOwner.isEmpty() || d->shuttingDown) {
                    return;
                }
                qCInfo(SURR_PORTAL) << "o portal reiniciou; refazendo as sessões";
                d->injector->detach();
                d->clipboard->clearSession();
                d->shortcuts->bind();
                QTimer::singleShot(kPortalWarmupMs, this,
                                   [this] { d->session->open(QString(), false); });
            });

    // ---- Máquina de estados -------------------------------------------
    connect(d->machine, &DictationStateMachine::startCaptureRequested, this, [this](quint64 id) {
        d->overlay->beginRecording();
        d->tray->setStatus(TrayIcon::Status::Recording);
        d->recorder->start(id);
    });
    connect(d->machine, &DictationStateMachine::stopCaptureRequested, d->recorder,
            &AudioRecorder::stop);
    connect(d->machine, &DictationStateMachine::abortCaptureRequested, d->recorder,
            &AudioRecorder::abort);

    connect(d->recorder, &AudioRecorder::captureStarted, this, [this](quint64) {
        d->overlay->markLive();
        d->machine->onCaptureStarted();
    });
    connect(d->recorder, &AudioRecorder::levelChanged, d->overlay, &OverlayController::setLevel);
    connect(d->recorder, &AudioRecorder::captureFailed, this,
            [this](quint64, const QString &message) { d->machine->onCaptureFailed(message); });
    connect(d->recorder, &AudioRecorder::captureFinished, this,
            [this](quint64, const QString &path, double seconds) {
                d->machine->onCaptureFinalized(path, seconds);
            });

    connect(d->machine, &DictationStateMachine::transcriptionRequested, this,
            [this](quint64, const QString &wav) {
                d->overlay->showTranscribing();
                d->tray->setStatus(TrayIcon::Status::Working);
                if (!d->active) {
                    d->machine->onTranscriptionFailed(
                        QStringLiteral("Nenhum serviço de transcrição configurado."));
                    return;
                }
                ITranscriptionProvider::Options options;
                options.language = d->settings->language();
                options.model = d->settings->provider() == QLatin1String(GroqProvider::kId)
                    ? d->settings->groqModel()
                    : QString();
                d->active->transcribe(wav, options);
            });
    connect(d->machine, &DictationStateMachine::transcriptionCancelRequested, this,
            [this](quint64) {
                if (d->active) {
                    d->active->cancel();
                }
            });

    auto wireProvider = [this](ITranscriptionProvider *provider) {
        connect(provider, &ITranscriptionProvider::finished, this,
                [this](const TranscriptionResult &result) {
                    cleanupAudio();
                    d->machine->onTranscriptionSucceeded(result.text);
                });
        connect(provider, &ITranscriptionProvider::failed, this,
                [this](const TranscriptionFailure &failure) {
                    cleanupAudio();
                    d->machine->onTranscriptionFailed(failure.message);
                });
    };
    wireProvider(d->groq);
    wireProvider(d->cepify);

    connect(d->machine, &DictationStateMachine::deliveryRequested, d->delivery,
            &DeliveryCoordinator::deliver);
    connect(d->delivery, &DeliveryCoordinator::finished, this,
            [this](quint64, bool pasted, const QString &note) {
                if (!note.isEmpty()) {
                    d->overlay->showSuccess(note);
                } else {
                    d->overlay->showSuccess(QString());
                }
                d->machine->onDeliveryFinished(pasted);
            });
    connect(d->delivery, &DeliveryCoordinator::failed, this,
            [this](quint64, const QString &message) { d->machine->onDeliveryFailed(message); });

    connect(d->machine, &DictationStateMachine::cycleCompleted, this,
            [this](quint64, const QString &, bool) {
                d->tray->setStatus(TrayIcon::Status::Idle);
            });
    connect(d->machine, &DictationStateMachine::cycleAborted, this,
            [this](quint64, DictationStateMachine::AbortReason reason, const QString &message) {
                cleanupAudio();
                d->tray->setStatus(TrayIcon::Status::Idle);
                using Reason = DictationStateMachine::AbortReason;
                if (reason == Reason::ShuttingDown) {
                    return;
                }
                if (message.isEmpty()) {
                    d->overlay->dismiss();
                    return;
                }
                // Erro nunca vira texto colado no documento do usuário: fica
                // só na cápsula.
                d->overlay->showError(message);
            });

    // ---- Bandeja -------------------------------------------------------
    connect(d->tray, &TrayIcon::settingsRequested, this, &ApplicationController::openSettings);
    connect(d->tray, &TrayIcon::authorizePasteRequested, this,
            &ApplicationController::authorizePaste);
    connect(d->tray, &TrayIcon::autostartToggled, this, [this](bool enabled) {
        d->settings->setAutostart(enabled);
        d->settings->save();
    });
    connect(d->tray, &TrayIcon::quitRequested, this, &ApplicationController::shutdown);

    // Caminho próprio: /MainApplication já pertence ao KDBusService
    // (interface org.freedesktop.Application), e registrar por cima falha.
    if (!QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/Control"), QStringLiteral("br.com.cepify.Surr.Control"), this,
            QDBusConnection::ExportScriptableSlots)) {
        qCWarning(SURR_APP) << "não foi possível exportar a interface de controle";
    }

    qCInfo(SURR_APP) << "surr" << SURR_VERSION_STRING << "pronto";
    return true;
}

void ApplicationController::cleanupAudio()
{
    if (d->settings->debugKeepAudio()) {
        return;
    }
    // Não deixar áudio do usuário para trás.
    const QString path = d->recorder->currentPath();
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
}

void ApplicationController::openSettings()
{
    // Processo separado de propósito: este processo chamou
    // LayerShellQt::Shell::useLayerShell(), que transforma TODA janela sua em
    // superfície de layer-shell — o que arruinaria uma janela de
    // configurações normal (sem decoração, ancorada, sem barra de tarefas).
    QProcess::startDetached(QCoreApplication::applicationFilePath(),
                            {QStringLiteral("--settings")});
}

void ApplicationController::authorizePaste()
{
    if (d->machine->isBusy()) {
        d->tray->notify(QStringLiteral("Surr"),
                        QStringLiteral("Aguarde o ditado terminar para reconfigurar a permissão."));
        return;
    }
    d->session->close();
    d->session->open(QString(), /*interactive=*/true);
}

void ApplicationController::configureShortcut()
{
    // Quem decide o gatilho é o compositor; isto abre a tela do sistema
    // onde o usuário troca a tecla.
    d->shortcuts->configure();
}

QString ApplicationController::triggerDescription() const
{
    return d->shortcuts ? d->shortcuts->triggerDescription() : QString();
}

void ApplicationController::shutdown()
{
    d->shuttingDown = true;
    d->machine->requestShutdown();
    cleanupAudio();
    d->injector->detach();
    d->session->close();
    d->settings->save();
    QCoreApplication::quit();
}

} // namespace surr
