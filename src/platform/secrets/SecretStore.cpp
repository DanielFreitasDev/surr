#include "SecretStore.h"

#include "SurrDebug.h"

#include <KWallet>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariantMap>

namespace surr {

namespace {
constexpr const char *kFolder = "surr";

QString fallbackPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/secrets.json");
}
} // namespace

struct SecretStore::Private {
    KWallet::Wallet *wallet = nullptr;
    bool fallback = false;
    QVariantMap fallbackData;

    void loadFallback()
    {
        QFile file(fallbackPath());
        if (file.open(QIODevice::ReadOnly)) {
            fallbackData = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        }
    }
};

SecretStore::SecretStore(QObject *parent) : QObject(parent), d(new Private) {}

SecretStore::~SecretStore()
{
    delete d->wallet;
    delete d;
}

bool SecretStore::isOpen() const
{
    return (d->wallet && d->wallet->isOpen()) || d->fallback;
}

bool SecretStore::isUsingFallback() const { return d->fallback; }

void SecretStore::open()
{
    if (isOpen()) {
        Q_EMIT readyChanged(true);
        return;
    }

    // Assíncrono de propósito: a variante síncrona roda um event loop
    // aninhado, e um event loop aninhado quebra a ordem de entrega dos
    // sinais do portal — que é o que garante press/release corretos.
    d->wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0,
                                            KWallet::Wallet::Asynchronous);
    if (!d->wallet) {
        qCWarning(SURR_APP) << "KWallet indisponível; usando arquivo local 0600";
        d->fallback = true;
        d->loadFallback();
        Q_EMIT unavailable(QStringLiteral("O KWallet não está disponível."));
        Q_EMIT readyChanged(true);
        return;
    }

    connect(d->wallet, &KWallet::Wallet::walletOpened, this, [this](bool ok) {
        if (!ok) {
            qCWarning(SURR_APP) << "a carteira não abriu";
            d->fallback = true;
            d->loadFallback();
            Q_EMIT unavailable(
                QStringLiteral("Não foi possível abrir a carteira do KDE."));
            Q_EMIT readyChanged(true);
            return;
        }

        if (!d->wallet->hasFolder(QLatin1String(kFolder))) {
            d->wallet->createFolder(QLatin1String(kFolder));
        }
        d->wallet->setFolder(QLatin1String(kFolder));
        qCInfo(SURR_APP) << "carteira aberta";
        Q_EMIT readyChanged(true);
    });
}

void SecretStore::refresh()
{
    // No KWallet cada read() já é uma ida ao kwalletd, então não há cópia a
    // atualizar. O refúgio em disco é que mora em memória desde o open().
    if (d->fallback) {
        d->loadFallback();
    }
}

QString SecretStore::read(const QString &key) const
{
    if (d->fallback) {
        return d->fallbackData.value(key).toString();
    }
    if (!d->wallet || !d->wallet->isOpen()) {
        return {};
    }
    QString value;
    d->wallet->readPassword(key, value);
    return value;
}

bool SecretStore::write(const QString &key, const QString &value)
{
    if (d->fallback) {
        d->fallbackData.insert(key, value);
        const QString path = fallbackPath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.write(QJsonDocument(QJsonObject::fromVariantMap(d->fallbackData)).toJson());
        return true;
    }
    if (!d->wallet || !d->wallet->isOpen()) {
        return false;
    }
    return d->wallet->writePassword(key, value) == 0;
}

bool SecretStore::remove(const QString &key)
{
    if (d->fallback) {
        d->fallbackData.remove(key);
        return write(QString(), QString()) || true;
    }
    if (!d->wallet || !d->wallet->isOpen()) {
        return false;
    }
    return d->wallet->removeEntry(key) == 0;
}

bool SecretStore::has(const QString &key) const { return !read(key).isEmpty(); }

bool SecretStore::groqKeyComesFromEnvironment()
{
    return !qEnvironmentVariable("GROQ_API_KEY").isEmpty();
}

QString SecretStore::groqApiKey() const
{
    const QString fromEnv = qEnvironmentVariable("GROQ_API_KEY");
    if (!fromEnv.isEmpty()) {
        return fromEnv;
    }
    return read(QLatin1String(kGroqKey));
}

QString SecretStore::legacyConfigPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/sussurro/config.json");
}

QString SecretStore::findLegacyGroqKey()
{
    QFile file(legacyConfigPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    return root.value(QStringLiteral("api_key")).toString();
}

} // namespace surr
