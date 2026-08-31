#include "TrayIcon.h"

#include "surr_config.h"

#include <KLocalizedString>
#include <KNotification>
#include <KStatusNotifierItem>

#include <QAction>
#include <QMenu>

namespace surr {

struct TrayIcon::Private {
    KStatusNotifierItem *sni = nullptr;
    QAction *statusAction = nullptr;
    QAction *providerAction = nullptr;
    QAction *autostartAction = nullptr;
    QAction *authorizeAction = nullptr;
    QString providerName;
    QString trigger;
    QString detail;
    Status status = Status::Idle;
};

TrayIcon::TrayIcon(QObject *parent) : QObject(parent), d(new Private)
{
    d->sni = new KStatusNotifierItem(QStringLiteral(SURR_APP_ID), this);
    d->sni->setTitle(QStringLiteral(SURR_DISPLAY_NAME));
    d->sni->setCategory(KStatusNotifierItem::ApplicationStatus);
    d->sni->setStatus(KStatusNotifierItem::Passive);
    d->sni->setIconByName(QStringLiteral("audio-input-microphone"));

    auto *menu = new QMenu();

    // Primeira linha: o estado, só informativo.
    d->statusAction = menu->addAction(QString());
    d->statusAction->setEnabled(false);

    d->providerAction = menu->addAction(QString());
    d->providerAction->setEnabled(false);

    menu->addSeparator();

    QAction *settings = menu->addAction(QIcon::fromTheme(QStringLiteral("configure")),
                                        i18n("Configurações…"));
    connect(settings, &QAction::triggered, this, &TrayIcon::settingsRequested);

    d->authorizeAction = menu->addAction(QIcon::fromTheme(QStringLiteral("dialog-password")),
                                         i18n("Autorizar colagem automática…"));
    connect(d->authorizeAction, &QAction::triggered, this, &TrayIcon::authorizePasteRequested);

    d->autostartAction = menu->addAction(i18n("Iniciar com a sessão"));
    d->autostartAction->setCheckable(true);
    connect(d->autostartAction, &QAction::toggled, this, &TrayIcon::autostartToggled);

    menu->addSeparator();
    QAction *quit = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                    i18n("Sair"));
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);

    d->sni->setContextMenu(menu);
    // Clicar no ícone abre as Configurações; um app de ditado não tem
    // "janela principal" para restaurar.
    d->sni->setAssociatedWindow(nullptr);
    connect(d->sni, &KStatusNotifierItem::activateRequested, this,
            [this](bool, const QPoint &) { Q_EMIT settingsRequested(); });

    setStatus(Status::Idle);
}

TrayIcon::~TrayIcon() { delete d; }

void TrayIcon::setStatus(Status status, const QString &detail)
{
    d->status = status;
    d->detail = detail;

    switch (status) {
    case Status::Idle:
        d->sni->setIconByName(QStringLiteral("audio-input-microphone"));
        d->sni->setStatus(KStatusNotifierItem::Passive);
        d->statusAction->setText(i18n("Pronto"));
        break;
    case Status::Recording:
        // Ativo enquanto grava: o ícone fica destacado na bandeja.
        d->sni->setIconByName(QStringLiteral("media-record"));
        d->sni->setStatus(KStatusNotifierItem::Active);
        d->statusAction->setText(i18n("Gravando…"));
        break;
    case Status::Working:
        d->sni->setIconByName(QStringLiteral("view-refresh"));
        d->sni->setStatus(KStatusNotifierItem::Active);
        d->statusAction->setText(i18n("Transcrevendo…"));
        break;
    case Status::Error:
        d->sni->setIconByName(QStringLiteral("dialog-warning"));
        d->sni->setStatus(KStatusNotifierItem::NeedsAttention);
        d->statusAction->setText(detail.isEmpty() ? i18n("Erro") : detail);
        break;
    case Status::Unconfigured:
        d->sni->setIconByName(QStringLiteral("dialog-warning"));
        d->sni->setStatus(KStatusNotifierItem::NeedsAttention);
        d->statusAction->setText(i18n("Configuração pendente"));
        break;
    }
    rebuildTooltip();
}

void TrayIcon::setProviderName(const QString &name)
{
    d->providerName = name;
    d->providerAction->setText(i18n("Serviço: %1", name));
    rebuildTooltip();
}

void TrayIcon::setTriggerDescription(const QString &trigger)
{
    d->trigger = trigger;
    rebuildTooltip();
}

void TrayIcon::setAutostartEnabled(bool enabled)
{
    QSignalBlocker blocker(d->autostartAction);
    d->autostartAction->setChecked(enabled);
}

void TrayIcon::setPasteAuthorized(bool authorized)
{
    d->authorizeAction->setText(authorized ? i18n("Reconfigurar colagem automática…")
                                           : i18n("Autorizar colagem automática…"));
}

void TrayIcon::rebuildTooltip()
{
    // O gatilho real vem do compositor, não está cravado no código: o
    // usuário pode ter trocado a tecla pelas Configurações do sistema.
    const QString hint = d->trigger.isEmpty()
        ? i18n("Atalho não registrado")
        : i18n("Segure %1 para ditar", d->trigger);
    d->sni->setToolTip(d->sni->iconName(), QStringLiteral(SURR_DISPLAY_NAME), hint);
}

void TrayIcon::notify(const QString &title, const QString &text, bool isError)
{
    auto *notification = new KNotification(
        isError ? QStringLiteral("error") : QStringLiteral("notification"),
        KNotification::CloseOnTimeout);
    notification->setTitle(title);
    notification->setText(text);
    notification->setIconName(isError ? QStringLiteral("dialog-error")
                                      : QStringLiteral("audio-input-microphone"));
    notification->sendEvent();
}

} // namespace surr
