#include "PortalTypes.h"

#include <QDBusMetaType>

namespace surr {

QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &shortcut)
{
    arg.beginStructure();
    arg << shortcut.id << shortcut.meta;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &shortcut)
{
    arg.beginStructure();
    arg >> shortcut.id >> shortcut.meta;
    arg.endStructure();
    return arg;
}

void registerPortalTypes()
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcutList>();
}

} // namespace surr
