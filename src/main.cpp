#include "ApplicationController.h"
#include "OverlayController.h"
#include "SettingsWindow.h"
#include "SurrDebug.h"
#include "PortalTypes.h"
#include "surr_config.h"

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <QIcon>
#include <QRandomGenerator>
#include <QTimer>

#if __has_include(<LayerShellQt/Shell>)
#include <LayerShellQt/Shell>
#define SURR_HAS_LAYERSHELL 1
#endif

namespace {

/**
 * Declara o app_id ao portal.
 *
 * PRECISA ser a primeira chamada ao portal e só pode acontecer uma vez. Sem
 * ela, o portal deduz a identidade pelo systemd scope de quem lançou o
 * processo — lançado de um terminal, o aplicativo herdaria a identidade do
 * terminal, e a permissão de colagem ficaria guardada no nome errado.
 *
 * Exige também que exista um `.desktop` com este nome e cujo `Exec` resolva;
 * caso contrário o portal responde "App info not found".
 */
void registerAppId()
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.host.portal.Registry"), QStringLiteral("Register"));
    message.setArguments({QStringLiteral(SURR_APP_ID), QVariantMap{}});

    // Bloqueio curto e deliberado: é a única chamada síncrona do processo, e
    // precisa terminar antes de qualquer outra conversa com o portal.
    const QDBusMessage reply =
        QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(SURR_APP) << "Registry.Register falhou:" << reply.errorMessage()
                            << "— as permissões podem ficar sob outro app_id";
    } else {
        qCDebug(SURR_APP) << "app_id registrado:" << SURR_APP_ID;
    }
}

} // namespace

int main(int argc, char **argv)
{
    // O modo é decidido antes de criar QApplication porque só o processo
    // daemon pode ligar o layer-shell.
    bool settingsMode = false;
    bool previewMode = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--settings") == 0) {
            settingsMode = true;
        } else if (qstrcmp(argv[i], "--preview") == 0) {
            previewMode = true;
        }
    }

#ifdef SURR_HAS_LAYERSHELL
    if (!settingsMode) {
        // Vale para o processo inteiro: toda janela vira superfície de
        // layer-shell. Por isso o daemon tem só a cápsula, e as
        // Configurações rodam noutro processo.
        LayerShellQt::Shell::useLayerShell();
    }
#endif

    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral(SURR_APP_ID));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("audio-input-microphone")));
    // Fechar a janela de configurações não pode encerrar o processo daemon.
    QApplication::setQuitOnLastWindowClosed(false);

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("surr"));

    KAboutData about(QStringLiteral(SURR_APP_NAME), i18n("Surr"),
                     QStringLiteral(SURR_VERSION_STRING),
                     i18n("Ditado por voz push-to-talk para o KDE Plasma"),
                     KAboutLicense::GPL_V3);
    about.setDesktopFileName(QStringLiteral(SURR_APP_ID));
    about.setOrganizationDomain(QByteArrayLiteral("cepify.com.br"));
    KAboutData::setApplicationData(about);

    // KAboutData::setApplicationData carimba organizationDomain = kde.org, e
    // o KDBusService monta o nome do barramento a partir dele — sairia
    // "org.kde.surr". Esse namespace é da KDE, não deste projeto, então o
    // domínio é reposto DEPOIS da chamada acima para o nome virar
    // br.com.cepify.Surr, igual ao app_id do portal e ao .desktop.
    QCoreApplication::setOrganizationDomain(QStringLiteral("cepify.com.br"));
    QCoreApplication::setApplicationName(QStringLiteral("Surr"));

    QCommandLineParser parser;
    parser.setApplicationDescription(about.shortDescription());
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption settingsOption(QStringLiteral("settings"),
                                      i18n("Abre a janela de configurações."));
    parser.addOption(settingsOption);
    QCommandLineOption previewOption(
        QStringLiteral("preview"),
        i18n("Mostra a cápsula por alguns segundos, sem gravar nem chamar a API."));
    parser.addOption(previewOption);
    parser.process(app);

    surr::registerPortalTypes();

    if (previewMode) {
        // Percorre os estados visuais sem gravar áudio nem gastar chamada de
        // API. Serve para conferir posicionamento (inclusive em vários
        // monitores) e aparência sem depender de falar.
        auto *overlay = new surr::OverlayController(&app);
        if (!overlay->initialize()) {
            return 1;
        }
        overlay->beginRecording();
        QTimer::singleShot(400, overlay, &surr::OverlayController::markLive);

        auto *levels = new QTimer(&app);
        levels->setInterval(60);
        QObject::connect(levels, &QTimer::timeout, overlay, [overlay] {
            overlay->setLevel(0.25f + 0.6f * float(QRandomGenerator::global()->generateDouble()));
        });
        levels->start();

        QTimer::singleShot(4500, overlay, [overlay, levels] {
            levels->stop();
            overlay->showTranscribing();
        });
        QTimer::singleShot(6000, overlay, [overlay] {
            overlay->showSuccess(QStringLiteral("Pronto"));
        });
        QTimer::singleShot(8000, &app, &QCoreApplication::quit);
        return app.exec();
    }

    if (settingsMode) {
        // Processo de configurações: janela normal, sem layer-shell, e sem
        // instância única (o daemon é que é único).
        auto *window = new surr::SettingsWindow;
        window->show();
        QApplication::setQuitOnLastWindowClosed(true);
        return app.exec();
    }

    // Instância única do daemon. Uma segunda execução vira uma mensagem
    // para a que já roda, em vez de um segundo processo disputando a tecla.
    KDBusService service(KDBusService::Unique);

    registerAppId();

    auto *controller = new surr::ApplicationController(&app);
    QObject::connect(&service, &KDBusService::activateRequested, controller,
                     [controller](const QStringList &, const QString &) {
                         controller->openSettings();
                     });

    if (!controller->start()) {
        return 1;
    }
    return app.exec();
}
