#pragma once

#include <QObject>
#include <QString>

class KStatusNotifierItem;
class QAction;

namespace surr {

/**
 * Ícone de bandeja.
 *
 * `KStatusNotifierItem` e não `QSystemTrayIcon`: o SNI é baseado em D-Bus e
 * é o protocolo nativo da bandeja do Plasma no Wayland.
 */
class TrayIcon : public QObject
{
    Q_OBJECT
public:
    enum class Status { Idle, Recording, Working, Error, Unconfigured };

    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    void setStatus(Status status, const QString &detail = QString());
    void setProviderName(const QString &name);
    void setTriggerDescription(const QString &trigger);
    void setAutostartEnabled(bool enabled);
    void setPasteAuthorized(bool authorized);

    /// Notificação de sistema — usada para erros quando não há overlay.
    void notify(const QString &title, const QString &text, bool isError = false);

Q_SIGNALS:
    void settingsRequested();
    void authorizePasteRequested();
    void autostartToggled(bool enabled);
    void quitRequested();

private:
    void rebuildTooltip();

    struct Private;
    Private *d;
};

} // namespace surr
