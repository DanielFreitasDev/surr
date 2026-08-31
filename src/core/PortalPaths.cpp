#include "PortalPaths.h"

#include <QRandomGenerator>

namespace surr::PortalPaths {

QString sanitizeBusName(const QString &uniqueBusName)
{
    QString sender = uniqueBusName;
    if (sender.startsWith(QLatin1Char(':'))) {
        sender.remove(0, 1);
    }
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return sender;
}

QString requestPath(const QString &uniqueBusName, const QString &handleToken)
{
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
        .arg(sanitizeBusName(uniqueBusName), handleToken);
}

QString makeToken(const char *prefix)
{
    // Um elemento de object path aceita só [A-Za-z0-9_].
    return QStringLiteral("surr_%1_%2")
        .arg(QLatin1String(prefix))
        .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
}

} // namespace surr::PortalPaths
