#pragma once

#include <QDBusArgument>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace surr {

/**
 * Um item de `a(sa{sv})` — o tipo que `BindShortcuts` recebe e que
 * `ShortcutsChanged` devolve.
 *
 * O QtDBus não tem mapeamento embutido para struct(string, vardict), então
 * os operadores de streaming abaixo são obrigatórios. Sem eles a chamada
 * falha com um erro de assinatura que não diz o que está errado.
 */
struct PortalShortcut {
    QString id;
    QVariantMap meta; ///< description, preferred_trigger, trigger_description

    QString description() const
    {
        return meta.value(QStringLiteral("description")).toString();
    }
    /// Texto legível do gatilho real, decidido pelo compositor.
    QString triggerDescription() const
    {
        return meta.value(QStringLiteral("trigger_description")).toString();
    }
};

using PortalShortcutList = QList<PortalShortcut>;

QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &shortcut);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &shortcut);

/// Chamar uma vez na partida, antes de qualquer chamada ao portal.
void registerPortalTypes();

} // namespace surr

Q_DECLARE_METATYPE(surr::PortalShortcut)
Q_DECLARE_METATYPE(surr::PortalShortcutList)
