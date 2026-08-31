#include "Settings.h"

#include "SurrDebug.h"
#include "surr_config.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace surr {

namespace {
QString autostartFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/") + QStringLiteral(SURR_APP_ID) + QStringLiteral(".desktop");
}

QString installedDesktopPath()
{
    // Procura o .desktop instalado em vez de gerar um novo: assim o autostart
    // e o menu descrevem o mesmo aplicativo, e o app_id bate com o do portal.
    const QString name = QStringLiteral(SURR_APP_ID) + QStringLiteral(".desktop");
    return QStandardPaths::locate(QStandardPaths::ApplicationsLocation, name);
}
} // namespace

struct Settings::Private {
    KConfig config{QStringLiteral("surrrc")};
};

Settings::Settings(QObject *parent) : QObject(parent), d(new Private) {}
Settings::~Settings() { delete d; }

#define SURR_GET(group, key, def) \
    KConfigGroup(&d->config, QStringLiteral(group)).readEntry(QStringLiteral(key), def)
#define SURR_SET(group, key, value)                                          \
    do {                                                                     \
        KConfigGroup g(&d->config, QStringLiteral(group));                   \
        g.writeEntry(QStringLiteral(key), value);                            \
        Q_EMIT changed();                                                    \
    } while (false)

QString Settings::provider() const { return SURR_GET("Transcription", "provider", QStringLiteral("groq")); }
void Settings::setProvider(const QString &id) { SURR_SET("Transcription", "provider", id); }

QString Settings::groqModel() const
{
    return SURR_GET("Transcription", "groqModel", QStringLiteral("whisper-large-v3-turbo"));
}
void Settings::setGroqModel(const QString &m) { SURR_SET("Transcription", "groqModel", m); }

QString Settings::language() const { return SURR_GET("Transcription", "language", QStringLiteral("pt")); }
void Settings::setLanguage(const QString &c) { SURR_SET("Transcription", "language", c); }

QString Settings::cepifyBaseUrl() const
{
    return SURR_GET("Transcription", "cepifyBaseUrl", QStringLiteral("https://whisper.exemplo.com.br"));
}
void Settings::setCepifyBaseUrl(const QString &u) { SURR_SET("Transcription", "cepifyBaseUrl", u); }

QString Settings::hotwords() const { return SURR_GET("Transcription", "hotwords", QString()); }
void Settings::setHotwords(const QString &v) { SURR_SET("Transcription", "hotwords", v); }

QString Settings::inputDeviceId() const { return SURR_GET("Audio", "inputDevice", QString()); }
void Settings::setInputDeviceId(const QString &id) { SURR_SET("Audio", "inputDevice", id); }

int Settings::minimumHoldMs() const { return SURR_GET("Audio", "minimumHoldMs", 450); }
void Settings::setMinimumHoldMs(int ms) { SURR_SET("Audio", "minimumHoldMs", ms); }

int Settings::maximumSeconds() const { return SURR_GET("Audio", "maximumSeconds", 300); }
void Settings::setMaximumSeconds(int s) { SURR_SET("Audio", "maximumSeconds", s); }

QString Settings::preferredTrigger() const
{
    return SURR_GET("Shortcut", "preferredTrigger", QStringLiteral("Pause"));
}
void Settings::setPreferredTrigger(const QString &t) { SURR_SET("Shortcut", "preferredTrigger", t); }

bool Settings::autoPaste() const { return SURR_GET("Delivery", "autoPaste", true); }
void Settings::setAutoPaste(bool e) { SURR_SET("Delivery", "autoPaste", e); }

bool Settings::terminalPasteMode() const { return SURR_GET("Delivery", "terminalPaste", false); }
void Settings::setTerminalPasteMode(bool e) { SURR_SET("Delivery", "terminalPaste", e); }

int Settings::pasteDelayMs() const { return SURR_GET("Delivery", "pasteDelayMs", 90); }
void Settings::setPasteDelayMs(int ms) { SURR_SET("Delivery", "pasteDelayMs", ms); }

bool Settings::onboardingDone() const { return SURR_GET("App", "onboardingDone", false); }
void Settings::setOnboardingDone(bool v) { SURR_SET("App", "onboardingDone", v); }

bool Settings::debugKeepAudio() const { return SURR_GET("App", "debugKeepAudio", false); }
void Settings::setDebugKeepAudio(bool v) { SURR_SET("App", "debugKeepAudio", v); }

#undef SURR_GET
#undef SURR_SET

bool Settings::autostart() const { return QFile::exists(autostartFilePath()); }

void Settings::setAutostart(bool enabled)
{
    const QString target = autostartFilePath();
    if (!enabled) {
        QFile::remove(target);
        return;
    }

    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::remove(target);

    // Mecanismo XDG padrão — nada de gambiarra em .bashrc/.profile.
    const QString source = installedDesktopPath();
    if (!source.isEmpty() && QFile::copy(source, target)) {
        qCInfo(SURR_APP) << "autostart ativado a partir de" << source;
        return;
    }
    qCWarning(SURR_APP) << "não foi possível ativar o autostart: .desktop não encontrado";
}

void Settings::save() { d->config.sync(); }

} // namespace surr
