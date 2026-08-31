/**
 * Sonda de portal — valida no ambiente real as duas incógnitas que decidem
 * a arquitetura do surr, antes de construir em cima delas:
 *
 *  1. O `Deactivated` do portal GlobalShortcuts chega ao SOLTAR a tecla?
 *     Sem isso não existe push-to-talk, só disparo único.
 *  2. Quantos `Activated` chegam enquanto a tecla fica segurada?
 *     (o auto-repeat do KWin não deve virar várias gravações)
 *
 * Uso:  ./surr-probe        e então segure e solte a tecla do atalho.
 */
#include "PortalPaths.h"
#include "PortalRequest.h"
#include "PortalTypes.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QElapsedTimer>
#include <QTimer>

#include <cstdio>

using namespace surr;

namespace {
constexpr const char *kService = "org.freedesktop.portal.Desktop";
constexpr const char *kObject = "/org/freedesktop/portal/desktop";
constexpr const char *kShortcuts = "org.freedesktop.portal.GlobalShortcuts";
constexpr const char *kAppId = "br.com.cepify.Surr";

void say(const QString &s) { std::printf("%s\n", qPrintable(s)); std::fflush(stdout); }

class Probe : public QObject
{
    Q_OBJECT
public:
    void start()
    {
        registerPortalTypes();
        registerIdentity();
    }

private:
    void registerIdentity()
    {
        // A interface Registry fica no objeto principal do portal
        // (/org/freedesktop/portal/desktop), e NÃO num caminho próprio —
        // verificado por introspecção nesta máquina.
        // Precisa ser a PRIMEIRA chamada ao portal, no máximo uma vez. Sem
        // isso o app_id vem do systemd scope — que, rodando do terminal, é
        // o do Konsole (foi o que aconteceu com o app anterior).
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QString::fromLatin1(kService), QString::fromLatin1(kObject),
            QStringLiteral("org.freedesktop.host.portal.Registry"), QStringLiteral("Register"));
        msg.setArguments({QString::fromLatin1(kAppId), QVariantMap{}});

        auto *w = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(msg), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
            w->deleteLater();
            const QDBusPendingReply<> reply = *w;
            if (reply.isError()) {
                say(QStringLiteral("[Registry] %1: %2")
                        .arg(reply.error().name(), reply.error().message()));
            } else {
                say(QStringLiteral("[Registry] app_id registrado: %1")
                        .arg(QLatin1String(kAppId)));
            }
            createSession();
        });
    }

    void createSession()
    {
        // Assinar os sinais antes da sessão existir; filtramos por handle.
        auto bus = QDBusConnection::sessionBus();
        bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kObject),
                    QString::fromLatin1(kShortcuts), QStringLiteral("Activated"), this,
                    SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
        bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kObject),
                    QString::fromLatin1(kShortcuts), QStringLiteral("Deactivated"), this,
                    SLOT(onDeactivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));

        QVariantMap options;
        options.insert(QStringLiteral("session_handle_token"), PortalPaths::makeToken("sess"));

        auto *req = PortalRequest::call(QString::fromLatin1(kShortcuts),
                                        QStringLiteral("CreateSession"), {}, options, this);
        connect(req, &PortalRequest::finished, this, [this](uint code, const QVariantMap &r) {
            if (code != 0) {
                say(QStringLiteral("[CreateSession] recusado, código %1").arg(code));
                qApp->exit(1);
                return;
            }
            m_session = r.value(QStringLiteral("session_handle")).toString();
            say(QStringLiteral("[CreateSession] ok: %1").arg(m_session));
            bindShortcuts();
        });
        connect(req, &PortalRequest::failed, this, [](const QString &e) {
            say(QStringLiteral("[CreateSession] falhou: %1").arg(e));
            qApp->exit(1);
        });
    }

    void bindShortcuts()
    {
        PortalShortcut dictate;
        dictate.id = QStringLiteral("dictate");
        dictate.meta.insert(QStringLiteral("description"),
                            QStringLiteral("Segurar para ditar"));
        // Formato da spec Freedesktop Shortcuts: nome de keysym do xkbcommon
        // sem o prefixo XKB_KEY_, sensível a maiúsculas. É só uma sugestão —
        // quem decide o gatilho final é o compositor/usuário.
        dictate.meta.insert(QStringLiteral("preferred_trigger"), QStringLiteral("Pause"));

        PortalShortcutList list{dictate};

        auto *req = PortalRequest::call(
            QString::fromLatin1(kShortcuts), QStringLiteral("BindShortcuts"),
            {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(list),
             QString()},
            {}, this);

        connect(req, &PortalRequest::finished, this, [this](uint code, const QVariantMap &r) {
            if (code != 0) {
                say(QStringLiteral("[BindShortcuts] recusado, código %1 "
                                   "(1 = cancelado pelo usuário)").arg(code));
                qApp->exit(1);
                return;
            }
            const auto arg = r.value(QStringLiteral("shortcuts")).value<QDBusArgument>();
            PortalShortcutList bound;
            arg >> bound;
            say(QStringLiteral("[BindShortcuts] ok, %1 atalho(s):").arg(bound.size()));
            for (const auto &s : bound) {
                say(QStringLiteral("    id=%1  descrição=\"%2\"  gatilho=\"%3\"")
                        .arg(s.id, s.description(), s.triggerDescription()));
            }
            say(QString());
            say(QStringLiteral("==> Agora SEGURE a tecla do atalho por ~2 s e SOLTE."));
            say(QStringLiteral("    (Ctrl+C encerra)"));
            say(QString());
        });
        connect(req, &PortalRequest::failed, this, [](const QString &e) {
            say(QStringLiteral("[BindShortcuts] falhou: %1").arg(e));
            qApp->exit(1);
        });
    }

private Q_SLOTS:
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong ts,
                     const QVariantMap &)
    {
        if (session.path() != m_session) return;
        ++m_activations;
        if (m_activations == 1) {
            m_held.start();
            m_firstTs = ts;
        }
        say(QStringLiteral("  ACTIVATED   id=%1  ts=%2  (nº %3)")
                .arg(id).arg(ts).arg(m_activations));
    }

    void onDeactivated(const QDBusObjectPath &session, const QString &id, qulonglong ts,
                       const QVariantMap &)
    {
        if (session.path() != m_session) return;
        const qint64 localMs = m_held.isValid() ? m_held.elapsed() : -1;
        const qint64 portalMs = (m_firstTs && ts > m_firstTs)
            ? qint64((ts - m_firstTs) / 1000) : -1;

        say(QStringLiteral("  DEACTIVATED id=%1  ts=%2").arg(id).arg(ts));
        say(QString());
        say(QStringLiteral("  ===> RESULTADO: Deactivated CHEGOU ao soltar."));
        say(QStringLiteral("       Activated recebidos enquanto segurava: %1")
                .arg(m_activations));
        say(QStringLiteral("       Tempo segurado: %1 ms (local) / %2 ms (timestamps do portal)")
                .arg(localMs).arg(portalMs));
        say(QStringLiteral("       Push-to-talk pelo portal: %1")
                .arg(m_activations == 1 ? QStringLiteral("VIÁVEL")
                                        : QStringLiteral("VIÁVEL, mas exige colapsar repetições")));
        say(QString());
        m_activations = 0;
        m_firstTs = 0;
    }

private:
    QString m_session;
    int m_activations = 0;
    qulonglong m_firstTs = 0;
    QElapsedTimer m_held;
};
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("surr-probe"));

    Probe probe;
    probe.start();

    // Rede de segurança: a sonda é interativa, mas não deve ficar viva para sempre.
    QTimer::singleShot(180'000, &app, [] {
        say(QStringLiteral("\nTempo esgotado (3 min). Encerrando."));
        qApp->quit();
    });
    return app.exec();
}

#include "surr-probe.moc"
