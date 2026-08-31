#include "OverlayController.h"

#include "SurrDebug.h"

#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTimer>

#if __has_include(<LayerShellQt/Window>)
#include <LayerShellQt/Window>
#define SURR_HAS_LAYERSHELL 1
#endif

namespace surr {

namespace {
constexpr int kWidth = 340;
constexpr int kHeight = 76;
/// Distância da borda inferior, herdada do aplicativo anterior.
constexpr int kBottomMargin = 66;
constexpr int kSuccessDismissMs = 1100;
constexpr int kErrorDismissMs = 4200;
} // namespace

struct OverlayController::Private {
    QQuickView *view = nullptr;
    QTimer *dismissTimer = nullptr;
    QTimer *durationTimer = nullptr;
    QElapsedTimer clock;
    State state = State::Hidden;
    /// Tela em que a superfície foi criada. Guardada por nós porque, sob
    /// layer-shell, `QWindow::screen()` nem sempre reflete a saída real.
    QScreen *surfaceScreen = nullptr;
};

OverlayController::OverlayController(QObject *parent) : QObject(parent), d(new Private)
{
    // Monitor desconectado no meio do uso: esquecer a tela guardada força a
    // superfície a ser recriada na próxima gravação.
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        if (d->surfaceScreen == screen) {
            d->surfaceScreen = nullptr;
        }
    });

    d->dismissTimer = new QTimer(this);
    d->dismissTimer->setSingleShot(true);
    connect(d->dismissTimer, &QTimer::timeout, this, &OverlayController::dismiss);

    d->durationTimer = new QTimer(this);
    d->durationTimer->setInterval(100);
    connect(d->durationTimer, &QTimer::timeout, this, [this] {
        if (d->view && d->view->rootObject()) {
            d->view->rootObject()->setProperty(
                "seconds", d->clock.isValid() ? d->clock.elapsed() / 1000.0 : 0.0);
        }
    });
}

OverlayController::~OverlayController()
{
    delete d->view;
    delete d;
}

bool OverlayController::initialize()
{
    d->view = new QQuickView;
    d->view->setResizeMode(QQuickView::SizeRootObjectToView);
    d->view->setColor(Qt::transparent);
    d->view->resize(kWidth, kHeight);

    // Nem foco, nem entrada de mouse: a cápsula é puramente informativa.
    d->view->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                      | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput
                      | Qt::Tool);

    d->view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/surr/qml/Overlay.qml")));
    if (d->view->status() != QQuickView::Ready) {
        qCWarning(SURR_UI) << "não foi possível carregar o QML da cápsula:"
                           << d->view->errors();
        return false;
    }

    applyLayerShell();
    return true;
}

void OverlayController::applyLayerShell()
{
#ifdef SURR_HAS_LAYERSHELL
    if (auto *layer = LayerShellQt::Window::get(d->view)) {
        // Overlay fica acima das janelas normais e do painel.
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        // O ponto mais importante do arquivo: sem teclado, o Ctrl+V da
        // entrega vai para o aplicativo do usuário, não para cá.
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // Ancorar só embaixo; ancorar nas quatro bordas (o padrão) faria a
        // cápsula esticar pela tela inteira.
        layer->setAnchors(LayerShellQt::Window::AnchorBottom);
        // Zona exclusiva zero: não reserva espaço, não empurra maximizadas.
        layer->setExclusiveZone(0);
        layer->setMargins({0, 0, 0, kBottomMargin});
        layer->setDesiredSize(QSize(kWidth, kHeight));
        layer->setActivateOnShow(false);
        layer->setScope(QStringLiteral("surr-overlay"));
        // Deliberadamente FALSE. Com true, o LayerShellQt passa uma saída
        // nula no get_layer_surface e deixa o KWin escolher a "tela ativa"
        // — que é a da janela em foco, não a do cursor, e que na prática
        // prendia a cápsula sempre no mesmo monitor. Aqui a tela é escolhida
        // explicitamente em `moveToActiveScreen()`.
        layer->setWantsToBeOnActiveScreen(false);
    } else {
        qCWarning(SURR_UI) << "LayerShellQt indisponível; a cápsula pode ficar mal posicionada";
    }
#endif
}

void OverlayController::showOnActiveScreen()
{
    if (!d->view) {
        return;
    }

    // `QCursor::pos()` NÃO funciona no Wayland: o compositor não expõe a
    // posição global do ponteiro aos clientes, e o Qt devolve (0,0) — foi o
    // que fez a cápsula ficar presa no primeiro monitor. O caminho suportado
    // no Plasma é perguntar ao próprio KWin qual é a saída ativa.
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"), QStringLiteral("activeOutputName"));

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message, 500), this);

    // Assíncrono de propósito: uma chamada bloqueante aqui rodaria um event
    // loop aninhado e poderia reordenar os sinais de press/release do portal.
    // A resposta local chega em poucos milissegundos, bem antes dos ~140 ms
    // que o microfone leva para entregar a primeira amostra.
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;

        QScreen *target = nullptr;
        if (!reply.isError()) {
            const QString name = reply.value();
            const auto screens = QGuiApplication::screens();
            for (QScreen *screen : screens) {
                if (screen->name() == name) {
                    target = screen;
                    break;
                }
            }
            if (!target) {
                qCDebug(SURR_UI) << "KWin indicou a saída" << name
                                 << "mas o Qt não tem QScreen com esse nome";
            }
        } else {
            // Fora do Plasma o método não existe; no X11 o cursor funciona.
            qCDebug(SURR_UI) << "activeOutputName indisponível:" << reply.error().name();
            target = QGuiApplication::screenAt(QCursor::pos());
        }

        placeOn(target ? target : QGuiApplication::primaryScreen());
        d->view->show();
        d->durationTimer->start();
    });
}

void OverlayController::placeOn(QScreen *screen)
{
    if (!screen || !d->view) {
        return;
    }

#ifdef SURR_HAS_LAYERSHELL
    if (auto *layer = LayerShellQt::Window::get(d->view)) {
        // No layer-shell a saída é escolhida quando a superfície é criada;
        // chamar setScreen() numa janela já criada não move nada. Destruir a
        // superfície (a cápsula está oculta aqui) força o compositor a
        // recriá-la na saída certa no próximo show().
        if (d->surfaceScreen != screen) {
            qCDebug(SURR_UI) << "cápsula muda de tela:"
                             << (d->surfaceScreen ? d->surfaceScreen->name()
                                                  : QStringLiteral("(nenhuma)"))
                             << "->" << screen->name();
            d->view->destroy();
            d->view->setScreen(screen);
            layer->setScreen(screen);
            d->surfaceScreen = screen;
        }
        return; // as margens do layer-shell cuidam da posição
    }
#endif

    // Sem layer-shell (X11, ou compositor sem o protocolo) a janela ainda
    // escolhe a própria posição.
    d->surfaceScreen = screen;
    d->view->setScreen(screen);
    const QRect geometry = screen->availableGeometry();
    d->view->setPosition(geometry.x() + (geometry.width() - kWidth) / 2,
                         geometry.y() + geometry.height() - kHeight - kBottomMargin);
}

void OverlayController::setState(State state)
{
    d->state = state;
    if (d->view && d->view->rootObject()) {
        d->view->rootObject()->setProperty("state", static_cast<int>(state));
    }
}

void OverlayController::beginRecording()
{
    if (!d->view) {
        return;
    }
    d->dismissTimer->stop();
    d->clock.start();

    if (auto *root = d->view->rootObject()) {
        root->setProperty("message", QString());
        root->setProperty("seconds", 0.0);
        root->setProperty("level", 0.0);
    }
    setState(State::Preparing);
    // O show() acontece quando o KWin responder qual é a tela ativa.
    showOnActiveScreen();
}

void OverlayController::markLive() { setState(State::Recording); }

void OverlayController::setLevel(float level)
{
    if (d->view && d->view->rootObject()) {
        d->view->rootObject()->setProperty("level", level);
    }
}

void OverlayController::showTranscribing()
{
    d->durationTimer->stop();
    setState(State::Transcribing);
}

void OverlayController::showSuccess(const QString &message)
{
    d->durationTimer->stop();
    if (d->view && d->view->rootObject()) {
        d->view->rootObject()->setProperty("message", message);
    }
    setState(State::Success);
    d->dismissTimer->start(kSuccessDismissMs);
}

void OverlayController::showError(const QString &message)
{
    d->durationTimer->stop();
    if (d->view && d->view->rootObject()) {
        d->view->rootObject()->setProperty("message", message);
    }
    setState(State::Error);
    // Erro fica mais tempo: precisa dar para ler.
    d->dismissTimer->start(kErrorDismissMs);
}

void OverlayController::dismiss()
{
    d->dismissTimer->stop();
    d->durationTimer->stop();
    setState(State::Hidden);
    if (d->view) {
        d->view->hide();
    }
}

} // namespace surr
