#include "ClipboardManager.h"

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
constexpr const char *kClipboard = "org.freedesktop.portal.Clipboard";

/// Ordem importa: os clientes pegam o primeiro tipo que entendem.
QStringList textMimeTypes()
{
    return {QStringLiteral("text/plain;charset=utf-8"), QStringLiteral("text/plain"),
            QStringLiteral("UTF8_STRING"), QStringLiteral("TEXT"), QStringLiteral("STRING")};
}
} // namespace

struct ClipboardManager::Private {
    QString session;
    QString text;
    bool connected = false;
};

ClipboardManager::ClipboardManager(QObject *parent) : QObject(parent), d(new Private) {}
ClipboardManager::~ClipboardManager() { delete d; }

bool ClipboardManager::isAvailable() const { return !d->session.isEmpty(); }
QString ClipboardManager::currentText() const { return d->text; }

void ClipboardManager::setSession(const QString &sessionHandle)
{
    d->session = sessionHandle;
    if (!d->connected && !sessionHandle.isEmpty()) {
        QDBusConnection::sessionBus().connect(
            QString::fromLatin1(kService), QString::fromLatin1(kObject),
            QString::fromLatin1(kClipboard), QStringLiteral("SelectionTransfer"), this,
            SLOT(onSelectionTransfer(QDBusObjectPath, QString, uint)));
        d->connected = true;
    }
}

void ClipboardManager::clearSession() { d->session.clear(); }

void ClipboardManager::setText(const QString &text)
{
    if (d->session.isEmpty()) {
        Q_EMIT failed(QStringLiteral("Sessão de área de transferência indisponível."));
        return;
    }

    d->text = text;

    QVariantMap options;
    options.insert(QStringLiteral("mime_types"), textMimeTypes());

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kObject),
        QString::fromLatin1(kClipboard), QStringLiteral("SetSelection"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(d->session)), options});

    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCWarning(SURR_PORTAL) << "SetSelection falhou:" << reply.error().name();
            Q_EMIT failed(reply.error().message());
            return;
        }
        // Nunca registrar o texto: é fala do usuário.
        qCDebug(SURR_PORTAL) << "seleção publicada," << d->text.size() << "caracteres";
        Q_EMIT succeeded();
    });
}

void ClipboardManager::onSelectionTransfer(const QDBusObjectPath &session,
                                           const QString &mimeType, uint serial)
{
    if (session.path() != d->session) {
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kObject),
        QString::fromLatin1(kClipboard), QStringLiteral("SelectionWrite"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(d->session)),
                      QVariant::fromValue(serial)});

    auto *w = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, serial, mimeType] {
        w->deleteLater();
        const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *w;
        bool ok = false;

        if (!reply.isError()) {
            const int fd = reply.value().fileDescriptor();
            const QByteArray payload = d->text.toUtf8();
            qint64 written = 0;
            ok = true;
            while (written < payload.size()) {
                const ssize_t n =
                    ::write(fd, payload.constData() + written, payload.size() - written);
                if (n <= 0) {
                    ok = false;
                    break;
                }
                written += n;
            }
        } else {
            qCWarning(SURR_PORTAL) << "SelectionWrite falhou:" << reply.error().name();
        }

        QDBusMessage done = QDBusMessage::createMethodCall(
            QString::fromLatin1(kService), QString::fromLatin1(kObject),
            QString::fromLatin1(kClipboard), QStringLiteral("SelectionWriteDone"));
        done.setArguments({QVariant::fromValue(QDBusObjectPath(d->session)),
                           QVariant::fromValue(serial), ok});
        QDBusConnection::sessionBus().asyncCall(done);

        qCDebug(SURR_PORTAL) << "transferência de seleção" << mimeType << (ok ? "ok" : "falhou");
    });
}

} // namespace surr
