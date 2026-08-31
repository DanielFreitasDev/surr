#include "RemoteDesktopSession.h"

#include "PortalPaths.h"
#include "PortalRequest.h"
#include "SurrDebug.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>

#include <unistd.h>

namespace surr {

namespace {
constexpr const char *kService = "org.freedesktop.portal.Desktop";
constexpr const char *kObject = "/org/freedesktop/portal/desktop";
constexpr const char *kRemoteDesktop = "org.freedesktop.portal.RemoteDesktop";
constexpr const char *kClipboard = "org.freedesktop.portal.Clipboard";
constexpr const char *kSessionIface = "org.freedesktop.portal.Session";

constexpr uint kDeviceKeyboard = 1;
constexpr uint kPersistUntilRevoked = 2;
} // namespace

struct RemoteDesktopSession::Private {
    QString sessionHandle;
    QString restoreToken;
    QString parentWindow;
    bool interactive = true;
    bool keyboard = false;
    bool clipboard = false;
    bool active = false;
    // `active` só vira true no fim da cadeia, quando o EIS conecta; até lá um
    // segundo open() passaria pela guarda e montaria uma sessão paralela — ver
    // o comentário do open().
    bool opening = false;
};

RemoteDesktopSession::RemoteDesktopSession(QObject *parent) : QObject(parent), d(new Private) {}

RemoteDesktopSession::~RemoteDesktopSession()
{
    close();
    delete d;
}

void RemoteDesktopSession::setRestoreToken(const QString &token) { d->restoreToken = token; }
QString RemoteDesktopSession::restoreToken() const { return d->restoreToken; }
bool RemoteDesktopSession::isActive() const { return d->active; }
bool RemoteDesktopSession::hasKeyboard() const { return d->keyboard; }
bool RemoteDesktopSession::hasClipboard() const { return d->clipboard; }
QString RemoteDesktopSession::sessionHandle() const { return d->sessionHandle; }

void RemoteDesktopSession::fail(const QString &message, bool userCancelled)
{
    qCWarning(SURR_PORTAL) << "RemoteDesktop:" << message;
    d->opening = false;
    d->active = false;
    Q_EMIT failed(message, userCancelled);
}

void RemoteDesktopSession::open(const QString &parentWindow, bool interactive)
{
    // Duas fontes pedem a sessão na partida: o aquecimento de quem abriu a
    // carteira e o vigia do portal. `active` sozinho não desempata — ele só
    // vira true depois de CreateSession → SelectDevices → RequestClipboard →
    // Start → ConnectToEIS, que no boot de 2026-08-31 levou 6 s. Nesse
    // intervalo os dois pedidos passaram, e o compositor recusou o EIS do
    // segundo com "Session is already connected", derrubando a permissão que
    // o primeiro tinha acabado de conquistar.
    if (d->active || d->opening) {
        return;
    }
    d->opening = true;
    d->parentWindow = parentWindow;
    d->interactive = interactive;
    d->keyboard = false;
    d->clipboard = false;

    if (!interactive && d->restoreToken.isEmpty()) {
        // Sem token não há como restaurar em silêncio, e abrir o diálogo
        // sozinho na partida roubaria o foco de quem está digitando.
        fail(QStringLiteral("Sem autorização salva para colagem automática."));
        return;
    }

    QVariantMap options;
    options.insert(QStringLiteral("session_handle_token"), PortalPaths::makeToken("rd"));

    auto *req = PortalRequest::call(QString::fromLatin1(kRemoteDesktop),
                                    QStringLiteral("CreateSession"), {}, options, this);
    connect(req, &PortalRequest::finished, this, [this](uint code, const QVariantMap &r) {
        if (code != 0) {
            fail(QStringLiteral("Não foi possível criar a sessão de entrada."), code == 1);
            return;
        }
        d->sessionHandle = r.value(QStringLiteral("session_handle")).toString();

        QDBusConnection::sessionBus().connect(
            QString::fromLatin1(kService), d->sessionHandle,
            QString::fromLatin1(kSessionIface), QStringLiteral("Closed"), this,
            SLOT(onSessionClosed(QVariantMap)));

        selectDevices();
    });
    connect(req, &PortalRequest::failed, this,
            [this](const QString &e) { fail(e); });
}

void RemoteDesktopSession::selectDevices()
{
    QVariantMap options;
    // `types` PRECISA ser marshalado como `u`. Omitir faz o KDE conceder
    // devices=0 — uma sessão sem teclado, que só falha lá na frente.
    options.insert(QStringLiteral("types"), QVariant::fromValue(kDeviceKeyboard));
    options.insert(QStringLiteral("persist_mode"), QVariant::fromValue(kPersistUntilRevoked));
    if (!d->restoreToken.isEmpty()) {
        options.insert(QStringLiteral("restore_token"), d->restoreToken);
    }

    auto *req = PortalRequest::call(
        QString::fromLatin1(kRemoteDesktop), QStringLiteral("SelectDevices"),
        {QVariant::fromValue(QDBusObjectPath(d->sessionHandle))}, options, this);

    connect(req, &PortalRequest::finished, this, [this](uint code, const QVariantMap &) {
        if (code != 0) {
            fail(QStringLiteral("A seleção de dispositivos foi recusada."), code == 1);
            return;
        }
        requestClipboard();
    });
    connect(req, &PortalRequest::failed, this, [this](const QString &e) { fail(e); });
}

void RemoteDesktopSession::requestClipboard()
{
    // Sem objeto Request: é chamada direta, e precisa acontecer entre o
    // SelectDevices e o Start.
    //
    // Por que pedir clipboard: um cliente Wayland nativo só consegue definir
    // a seleção com um serial de input de uma superfície com foco. A cápsula
    // do surr é layer-shell sem foco nenhum, então QClipboard::setText()
    // seria descartado em silêncio e o Ctrl+V colaria o conteúdo ANTERIOR.
    // O portal não tem essa exigência.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kObject),
        QString::fromLatin1(kClipboard), QStringLiteral("RequestClipboard"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(d->sessionHandle)), QVariantMap{}});

    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            // Não é fatal: dá para seguir sem clipboard pelo portal e cair
            // no QClipboard, que funciona quando há janela com foco.
            qCWarning(SURR_PORTAL)
                << "RequestClipboard recusado:" << reply.error().name();
        } else {
            d->clipboard = true;
        }
        start();
    });
}

void RemoteDesktopSession::start()
{
    auto *req = PortalRequest::call(
        QString::fromLatin1(kRemoteDesktop), QStringLiteral("Start"),
        {QVariant::fromValue(QDBusObjectPath(d->sessionHandle)), d->parentWindow}, {}, this);

    connect(req, &PortalRequest::finished, this, [this](uint code, const QVariantMap &r) {
        if (code != 0) {
            fail(code == 1 ? QStringLiteral("Permissão para colagem automática recusada.")
                           : QStringLiteral("Não foi possível iniciar a sessão de entrada."),
                 code == 1);
            return;
        }

        // O token é de uso único: o Start devolve um novo a cada restauração
        // bem-sucedida. Não gravar o novo perde a permissão na próxima vez.
        const QString token = r.value(QStringLiteral("restore_token")).toString();
        if (!token.isEmpty() && token != d->restoreToken) {
            d->restoreToken = token;
            Q_EMIT restoreTokenChanged(token);
        }

        const uint devices = r.value(QStringLiteral("devices")).toUInt();
        d->keyboard = (devices & kDeviceKeyboard) != 0;
        if (!d->keyboard) {
            fail(QStringLiteral("O portal não concedeu acesso ao teclado."));
            return;
        }
        if (r.contains(QStringLiteral("clipboard_enabled"))) {
            d->clipboard = r.value(QStringLiteral("clipboard_enabled")).toBool();
        }

        qCInfo(SURR_PORTAL) << "sessão RemoteDesktop iniciada; teclado=" << d->keyboard
                            << "clipboard=" << d->clipboard;
        connectToEis();
    });
    connect(req, &PortalRequest::failed, this, [this](const QString &e) { fail(e); });
}

void RemoteDesktopSession::connectToEis()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kObject),
        QString::fromLatin1(kRemoteDesktop), QStringLiteral("ConnectToEIS"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(d->sessionHandle)), QVariantMap{}});

    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *w;
        if (reply.isError()) {
            fail(QStringLiteral("O compositor recusou a conexão de entrada (EIS): %1")
                     .arg(reply.error().message()));
            return;
        }

        // O QDBusUnixFileDescriptor só EMPRESTA o descritor e o fecha ao ser
        // destruído; a libei toma posse do que recebe. Sem o dup() vira
        // use-after-close — e a falha aparece minutos depois, não na hora.
        const int borrowed = reply.value().fileDescriptor();
        const int owned = ::dup(borrowed);
        if (owned < 0) {
            fail(QStringLiteral("Não foi possível duplicar o descritor do EIS."));
            return;
        }

        d->opening = false;
        d->active = true;
        qCInfo(SURR_PORTAL) << "EIS conectado";
        Q_EMIT ready(owned);
    });
}

void RemoteDesktopSession::onSessionClosed(const QVariantMap &)
{
    qCInfo(SURR_PORTAL) << "sessão RemoteDesktop encerrada pelo portal";
    d->sessionHandle.clear();
    d->opening = false;
    d->active = false;
    d->keyboard = false;
    d->clipboard = false;
    Q_EMIT closed();
}

void RemoteDesktopSession::close()
{
    if (d->sessionHandle.isEmpty()) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), d->sessionHandle,
        QString::fromLatin1(kSessionIface), QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(msg);

    d->sessionHandle.clear();
    d->opening = false;
    d->active = false;
    d->keyboard = false;
    d->clipboard = false;
}

} // namespace surr
