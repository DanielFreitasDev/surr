/**
 * Sonda de colagem — valida a cadeia inteira de entrega no ambiente real:
 *
 *   Registry.Register → RemoteDesktop.CreateSession → SelectDevices(teclado)
 *   → RequestClipboard → Start → ConnectToEIS → SetSelection → Ctrl+V
 *
 * A incógnita principal é a área de transferência: um cliente Wayland nativo
 * sem janela em foco não consegue definir a seleção pelo caminho comum, e a
 * falha é silenciosa. Esta sonda prova se o caminho do portal resolve.
 *
 * Uso: ./surr-probe-paste
 *      Depois abra um editor de texto e deixe o cursor num campo.
 */
#include "ClipboardManager.h"
#include "EisInjector.h"
#include "RemoteDesktopSession.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

using namespace surr;

namespace {
void say(const QString &s) { std::printf("%s\n", qPrintable(s)); std::fflush(stdout); }

/// Texto de prova: acentos, cedilha, til, emoji e múltiplas linhas — tudo
/// que costuma quebrar quando se digita caractere a caractere.
QString proofText()
{
    return QStringLiteral(
        "Esse texto foi escrito usando o Surr\n"
        "Acentuação: ação, coração, português, ãõ, ç, áéíóú\n"
        "Símbolos e emoji: — ½ € 🎤 ✅");
}

QString tokenPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/remote-desktop.token");
}
} // namespace

class PasteProbe : public QObject
{
    Q_OBJECT
public:
    void start()
    {
        // Registry PRECISA vir antes de qualquer outra chamada ao portal.
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.host.portal.Registry"),
            QStringLiteral("Register"));
        msg.setArguments({QStringLiteral("br.com.cepify.Surr"), QVariantMap{}});

        auto *w = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
            w->deleteLater();
            const QDBusPendingReply<> reply = *w;
            say(reply.isError()
                    ? QStringLiteral("[Registry] %1").arg(reply.error().message())
                    : QStringLiteral("[Registry] app_id = br.com.cepify.Surr"));
            openSession();
        });
    }

private:
    void openSession()
    {
        m_session = new RemoteDesktopSession(this);

        QFile f(tokenPath());
        if (f.open(QIODevice::ReadOnly)) {
            const QString token = QString::fromUtf8(f.readAll()).trimmed();
            if (!token.isEmpty()) {
                m_session->setRestoreToken(token);
                say(QStringLiteral("[Token] restore_token existente encontrado"));
            }
        }

        connect(m_session, &RemoteDesktopSession::restoreTokenChanged, this,
                [](const QString &t) {
                    QFile out(tokenPath());
                    QDir().mkpath(QFileInfo(out).absolutePath());
                    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                        out.write(t.toUtf8());
                        say(QStringLiteral("[Token] novo restore_token gravado "
                                           "(uso único — sempre substituir)"));
                    }
                });

        connect(m_session, &RemoteDesktopSession::failed, this,
                [](const QString &m, bool cancelled) {
                    say(QStringLiteral("[Sessão] FALHOU: %1%2")
                            .arg(m, cancelled ? QStringLiteral(" (recusado pelo usuário)")
                                              : QString()));
                    qApp->exit(1);
                });

        connect(m_session, &RemoteDesktopSession::ready, this, &PasteProbe::onSessionReady);

        say(QStringLiteral("[Sessão] abrindo (pode aparecer um diálogo do KDE)…"));
        m_session->open(QString(), /*interactive=*/true);
    }

    void onSessionReady(int eisFd)
    {
        say(QStringLiteral("[Sessão] pronta — teclado=%1 clipboard=%2")
                .arg(m_session->hasKeyboard() ? QStringLiteral("sim") : QStringLiteral("NÃO"),
                     m_session->hasClipboard() ? QStringLiteral("sim") : QStringLiteral("NÃO")));

        m_clipboard = new ClipboardManager(this);
        m_clipboard->setSession(m_session->sessionHandle());

        m_injector = new EisInjector(this);
        connect(m_injector, &EisInjector::readyChanged, this, [this](bool ready) {
            say(QStringLiteral("[EIS] teclado virtual %1")
                    .arg(ready ? QStringLiteral("PRONTO") : QStringLiteral("indisponível")));
            if (ready && !m_armed) {
                m_armed = true;
                publishAndCountdown();
            }
        });
        connect(m_injector, &EisInjector::injected, this, [this](bool ok, const QString &msg) {
            say(ok ? QStringLiteral("[EIS] Ctrl+V enviado.")
                   : QStringLiteral("[EIS] falhou: %1").arg(msg));
            say(QString());
            say(QStringLiteral("==> Confira: o texto de prova apareceu onde o cursor estava?"));
            say(QStringLiteral("==> E pressione Ctrl+V manualmente em outro lugar: o mesmo"));
            say(QStringLiteral("    texto ainda deve estar na área de transferência."));
            say(QStringLiteral("    (esta sonda fica viva 60 s servindo o clipboard)"));
            QTimer::singleShot(60'000, qApp, &QCoreApplication::quit);
        });

        if (!m_injector->attach(eisFd)) {
            say(QStringLiteral("[EIS] não foi possível anexar o descritor"));
            qApp->exit(1);
        }
    }

    void publishAndCountdown()
    {
        connect(m_clipboard, &ClipboardManager::succeeded, this, [this] {
            say(QStringLiteral("[Clipboard] seleção publicada pelo portal."));
            say(QString());
            say(QStringLiteral("==> CLIQUE AGORA num campo de texto (Kate, Konsole, "
                               "navegador…)."));
            m_countdown = 14;
            tick();
        });
        connect(m_clipboard, &ClipboardManager::failed, this, [](const QString &m) {
            say(QStringLiteral("[Clipboard] FALHOU: %1").arg(m));
            qApp->exit(1);
        });
        m_clipboard->setText(proofText());
    }

    void tick()
    {
        if (--m_countdown <= 0) {
            say(QStringLiteral("    enviando Ctrl+V…"));
            m_injector->sendPaste();
            return;
        }
        say(QStringLiteral("    %1…").arg(m_countdown));
        QTimer::singleShot(1000, this, &PasteProbe::tick);
    }

    RemoteDesktopSession *m_session = nullptr;
    ClipboardManager *m_clipboard = nullptr;
    EisInjector *m_injector = nullptr;
    int m_countdown = 0;
    bool m_armed = false;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("surr"));
    QCoreApplication::setOrganizationName(QStringLiteral("surr"));

    PasteProbe probe;
    probe.start();

    QTimer::singleShot(300'000, &app, [] {
        say(QStringLiteral("\nTempo esgotado. Encerrando."));
        qApp->quit();
    });
    return app.exec();
}

#include "surr-probe-paste.moc"
