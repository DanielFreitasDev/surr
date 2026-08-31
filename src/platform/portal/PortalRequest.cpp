#include "PortalRequest.h"

#include "PortalPaths.h"
#include "SurrDebug.h"

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QTimer>

namespace surr {

namespace {
constexpr const char *kService = "org.freedesktop.portal.Desktop";
constexpr const char *kObject = "/org/freedesktop/portal/desktop";
constexpr const char *kRequestIface = "org.freedesktop.portal.Request";
constexpr int kDefaultTimeoutMs = 120'000;
} // namespace

struct PortalRequest::Private {
    QString predictedPath;
    QString actualPath;
    QTimer *timeout = nullptr;
    bool done = false;
};

PortalRequest::PortalRequest(QObject *parent) : QObject(parent), d(new Private) {}

void PortalRequest::setTimeout(int ms) { d->timeout->setInterval(ms); }

PortalRequest *PortalRequest::call(const QString &interface,
                                   const QString &method,
                                   const QVariantList &args,
                                   QVariantMap options,
                                   QObject *parent)
{
    auto *self = new PortalRequest(parent);
    auto bus = QDBusConnection::sessionBus();

    const QString token = PortalPaths::makeToken("req");
    options.insert(QStringLiteral("handle_token"), token);
    self->d->predictedPath = PortalPaths::requestPath(bus.baseService(), token);

    // Conectar ANTES de chamar. O portal pode emitir Response antes de a
    // chamada retornar, e o D-Bus não entrega sinais retroativamente.
    bus.connect(QString::fromLatin1(kService), self->d->predictedPath,
                QString::fromLatin1(kRequestIface), QStringLiteral("Response"), self,
                SLOT(onResponse(uint, QVariantMap)));

    self->d->timeout = new QTimer(self);
    self->d->timeout->setSingleShot(true);
    self->d->timeout->setInterval(kDefaultTimeoutMs);
    connect(self->d->timeout, &QTimer::timeout, self, [self] {
        if (!self->d->done) {
            self->d->done = true;
            Q_EMIT self->failed(QStringLiteral("O portal não respondeu a tempo."));
            self->deleteLater();
        }
    });
    self->d->timeout->start();

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kObject), interface, method);
    QVariantList allArgs = args;
    allArgs.append(QVariant::fromValue(options));
    msg.setArguments(allArgs);

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg, kDefaultTimeoutMs), self);
    connect(watcher, &QDBusPendingCallWatcher::finished, self,
            [self, watcher, interface, method] {
                watcher->deleteLater();
                const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
                if (reply.isError()) {
                    if (!self->d->done) {
                        self->d->done = true;
                        qCWarning(SURR_PORTAL)
                            << method << "falhou:" << reply.error().name();
                        Q_EMIT self->failed(reply.error().message());
                        self->deleteLater();
                    }
                    return;
                }

                // Se o portal devolveu um caminho diferente do previsto,
                // conecta também nele em vez de apostar na previsão.
                self->d->actualPath = reply.value().path();
                if (self->d->actualPath != self->d->predictedPath) {
                    qCDebug(SURR_PORTAL) << "caminho de Request divergiu do previsto";
                    QDBusConnection::sessionBus().connect(
                        QString::fromLatin1(kService), self->d->actualPath,
                        QString::fromLatin1(kRequestIface), QStringLiteral("Response"), self,
                        SLOT(onResponse(uint, QVariantMap)));
                }
            });

    return self;
}

void PortalRequest::onResponse(uint response, const QVariantMap &results)
{
    if (d->done) {
        return; // pode chegar duas vezes se os dois caminhos foram conectados
    }
    d->done = true;
    d->timeout->stop();
    Q_EMIT finished(response, results);
    deleteLater();
}

} // namespace surr
