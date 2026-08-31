#include "GlobalShortcutManager.h"

#include "PortalPaths.h"
#include "PortalRequest.h"
#include "PortalTypes.h"
#include "SurrDebug.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>

namespace surr {

namespace {
constexpr const char *kService = "org.freedesktop.portal.Desktop";
constexpr const char *kObject = "/org/freedesktop/portal/desktop";
constexpr const char *kIface = "org.freedesktop.portal.GlobalShortcuts";
constexpr const char *kSessionIface = "org.freedesktop.portal.Session";
} // namespace

struct GlobalShortcutManager::Private {
    QString session;
    QString preferredTrigger = QStringLiteral("Pause");
    QString triggerDescription;
    bool bound = false;
    bool keyDown = false;
    // Assinatura de sinal do barramento é match rule por nome de serviço:
    // sobrevive ao reinício do portal e só se faz uma vez.
    bool signalsConnected = false;
};

GlobalShortcutManager::GlobalShortcutManager(QObject *parent) : QObject(parent), d(new Private) {}

GlobalShortcutManager::~GlobalShortcutManager() { delete d; }

void GlobalShortcutManager::setPreferredTrigger(const QString &trigger)
{
    d->preferredTrigger = trigger;
}

bool GlobalShortcutManager::isBound() const { return d->bound; }
QString GlobalShortcutManager::triggerDescription() const { return d->triggerDescription; }

void GlobalShortcutManager::bind()
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        Q_EMIT unavailable(QStringLiteral("O barramento de sessão não está disponível."));
        return;
    }
    createSession();
}

void GlobalShortcutManager::createSession()
{
    auto bus = QDBusConnection::sessionBus();

    // Assinar antes de a sessão existir; o filtro por handle é feito nos slots.
    //
    // E uma vez só: o Qt recusa assinatura duplicada devolvendo `false`, então
    // um segundo bind() imprimia um aviso de falha que não era falha nenhuma —
    // foi o que apareceu no log do boot de 2026-08-31, ao lado do reinício
    // falso do portal.
    if (!d->signalsConnected) {
        const bool okActivated = bus.connect(
            QString::fromLatin1(kService), QString::fromLatin1(kObject),
            QString::fromLatin1(kIface), QStringLiteral("Activated"), this,
            SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
        const bool okDeactivated = bus.connect(
            QString::fromLatin1(kService), QString::fromLatin1(kObject),
            QString::fromLatin1(kIface), QStringLiteral("Deactivated"), this,
            SLOT(onDeactivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
        bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kObject),
                    QString::fromLatin1(kIface), QStringLiteral("ShortcutsChanged"), this,
                    SLOT(onShortcutsChanged(QDBusObjectPath, surr::PortalShortcutList)));

        if (!okActivated || !okDeactivated) {
            qCWarning(SURR_SHORTCUT)
                << "falha ao conectar sinais do portal: Activated=" << okActivated
                << "Deactivated=" << okDeactivated;
        } else {
            d->signalsConnected = true;
        }
    }

    QVariantMap options;
    options.insert(QStringLiteral("session_handle_token"), PortalPaths::makeToken("sc"));

    auto *req = PortalRequest::call(QString::fromLatin1(kIface), QStringLiteral("CreateSession"),
                                    {}, options, this);
    connect(req, &PortalRequest::finished, this, [this](uint code, const QVariantMap &r) {
        if (code != 0) {
            Q_EMIT unavailable(QStringLiteral("Não foi possível criar a sessão de atalhos."));
            return;
        }
        d->session = r.value(QStringLiteral("session_handle")).toString();
        QDBusConnection::sessionBus().connect(
            QString::fromLatin1(kService), d->session, QString::fromLatin1(kSessionIface),
            QStringLiteral("Closed"), this, SLOT(onSessionClosed(QVariantMap)));
        bindShortcuts();
    });
    connect(req, &PortalRequest::failed, this,
            [this](const QString &e) { Q_EMIT unavailable(e); });
}

void GlobalShortcutManager::bindShortcuts()
{
    PortalShortcut dictate;
    dictate.id = QString::fromLatin1(kDictateId);
    dictate.meta.insert(QStringLiteral("description"), QStringLiteral("Segurar para ditar"));
    if (!d->preferredTrigger.isEmpty()) {
        dictate.meta.insert(QStringLiteral("preferred_trigger"), d->preferredTrigger);
    }

    const PortalShortcutList shortcuts{dictate};

    auto *req = PortalRequest::call(
        QString::fromLatin1(kIface), QStringLiteral("BindShortcuts"),
        {QVariant::fromValue(QDBusObjectPath(d->session)), QVariant::fromValue(shortcuts),
         QString()},
        {}, this);

    connect(req, &PortalRequest::finished, this, [this](uint code, const QVariantMap &r) {
        if (code != 0) {
            Q_EMIT unavailable(code == 1
                                   ? QStringLiteral("O atalho global não foi autorizado.")
                                   : QStringLiteral("Não foi possível registrar o atalho."));
            return;
        }
        d->bound = true;
        PortalShortcutList bound;
        r.value(QStringLiteral("shortcuts")).value<QDBusArgument>() >> bound;
        applyShortcutList(bound);
        qCInfo(SURR_SHORTCUT) << "atalho registrado; gatilho:" << d->triggerDescription;
        Q_EMIT boundChanged(true);
    });
    connect(req, &PortalRequest::failed, this,
            [this](const QString &e) { Q_EMIT unavailable(e); });
}

void GlobalShortcutManager::applyShortcutList(const PortalShortcutList &list)
{
    for (const PortalShortcut &shortcut : list) {
        if (shortcut.id == QLatin1String(kDictateId)) {
            const QString description = shortcut.triggerDescription();
            if (description != d->triggerDescription) {
                d->triggerDescription = description;
                Q_EMIT triggerDescriptionChanged(description);
            }
        }
    }
}

void GlobalShortcutManager::configure(const QString &parentWindow)
{
    if (d->session.isEmpty()) {
        return;
    }
    // Sem objeto Request: é "dispare e esqueça" (novidade da v2 da interface).
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kObject),
        QString::fromLatin1(kIface), QStringLiteral("ConfigureShortcuts"));
    msg.setArguments({QVariant::fromValue(QDBusObjectPath(d->session)), parentWindow,
                      QVariantMap{}});
    QDBusConnection::sessionBus().asyncCall(msg);
}

void GlobalShortcutManager::onActivated(const QDBusObjectPath &session, const QString &id,
                                        qulonglong, const QVariantMap &)
{
    qCDebug(SURR_SHORTCUT) << "Activated recebido: id=" << id
                           << "sessão do sinal=" << session.path()
                           << "sessão nossa=" << d->session << "keyDown=" << d->keyDown;
    if (session.path() != d->session || id != QLatin1String(kDictateId)) {
        return;
    }
    // Guarda contra repetição: se o compositor um dia passar a repassar o
    // auto-repeat, ele não pode virar várias gravações.
    if (d->keyDown) {
        return;
    }
    d->keyDown = true;
    Q_EMIT pressed();
}

void GlobalShortcutManager::onDeactivated(const QDBusObjectPath &session, const QString &id,
                                          qulonglong, const QVariantMap &)
{
    qCDebug(SURR_SHORTCUT) << "Deactivated recebido: id=" << id
                           << "sessão do sinal=" << session.path()
                           << "sessão nossa=" << d->session << "keyDown=" << d->keyDown;
    if (session.path() != d->session || id != QLatin1String(kDictateId)) {
        return;
    }
    if (!d->keyDown) {
        return; // release sem press correspondente
    }
    d->keyDown = false;
    Q_EMIT released();
}

void GlobalShortcutManager::onShortcutsChanged(const QDBusObjectPath &session,
                                               const surr::PortalShortcutList &shortcuts)
{
    if (session.path() != d->session) {
        return;
    }
    applyShortcutList(shortcuts);
}

void GlobalShortcutManager::onSessionClosed(const QVariantMap &)
{
    qCWarning(SURR_SHORTCUT) << "a sessão de atalhos foi encerrada";
    d->session.clear();
    d->bound = false;
    d->keyDown = false;
    Q_EMIT boundChanged(false);
}

} // namespace surr
