#pragma once

#include "PortalTypes.h"

#include <QDBusObjectPath>
#include <QObject>
#include <QString>

namespace surr {

/**
 * Atalho global push-to-talk pelo portal GlobalShortcuts v2.
 *
 * Medido nesta máquina: `Activated` e `Deactivated` chegam ao pressionar e
 * ao soltar, e segurar 1,08 s produz **um** `Activated` — o KWin manda a
 * repetição por outro sinal, que o portal não repassa.
 *
 * Duas coisas que a medição também mostrou:
 *
 * - Os timestamps do portal vêm **zerados** nesta build do KDE. A duração da
 *   tecla é medida por `QElapsedTimer` local; usar o timestamp faria todo
 *   ditado ser lido como duração zero e descartado.
 * - Quem decide o gatilho final é o compositor. `preferred_trigger` é só uma
 *   sugestão, então a interface mostra `triggerDescription()`, nunca "Pause"
 *   cravado no código.
 */
class GlobalShortcutManager : public QObject
{
    Q_OBJECT
public:
    static constexpr const char *kDictateId = "dictate";

    explicit GlobalShortcutManager(QObject *parent = nullptr);
    ~GlobalShortcutManager() override;

    /// Sugestão inicial, no formato da spec Freedesktop Shortcuts.
    void setPreferredTrigger(const QString &trigger);

    void bind();

    /// Abre a tela do sistema para o usuário trocar o atalho.
    void configure(const QString &parentWindow = QString());

    bool isBound() const;
    /// Texto legível do gatilho realmente em vigor.
    QString triggerDescription() const;

Q_SIGNALS:
    void pressed();
    void released();
    void boundChanged(bool bound);
    void triggerDescriptionChanged(const QString &description);
    void unavailable(const QString &reason);

private Q_SLOTS:
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp,
                     const QVariantMap &options);
    void onDeactivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp,
                       const QVariantMap &options);
    void onShortcutsChanged(const QDBusObjectPath &session,
                            const surr::PortalShortcutList &shortcuts);
    void onSessionClosed(const QVariantMap &details);

private:
    void createSession();
    void bindShortcuts();
    void applyShortcutList(const PortalShortcutList &shortcuts);

    struct Private;
    Private *d;
};

} // namespace surr
