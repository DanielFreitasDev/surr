#include "SettingsWindow.h"

#include "AudioRecorder.h"
#include "CepifyProvider.h"
#include "GroqProvider.h"
#include "ProxyResolver.h"
#include "SecretStore.h"
#include "Settings.h"
#include "SurrDebug.h"
#include "surr_config.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QDir>
#include <QTemporaryFile>

#include "WavWriter.h"

namespace surr {

struct SettingsBackend::Private {
    Settings settings;
    SecretStore secrets;
    QNetworkAccessManager nam;
    QString status;
    QStringList deviceNames;
    QStringList deviceIds;
};

SettingsBackend::SettingsBackend(QObject *parent) : QObject(parent), d(new Private)
{
    ProxyResolver::applyToApplication();

    for (const auto &[id, name] : AudioRecorder::availableInputs()) {
        d->deviceIds.append(id);
        d->deviceNames.append(name);
    }

    connect(&d->secrets, &SecretStore::readyChanged, this, [this](bool) {
        Q_EMIT secretsChanged();
        if (d->secrets.isUsingFallback()) {
            setStatus(QStringLiteral("O KWallet não está disponível; as chaves ficam num "
                                     "arquivo local com permissão restrita."));
        }
    });
    d->secrets.open();
}

SettingsBackend::~SettingsBackend() { delete d; }

// Duas variantes porque QString entra por referência const e os escalares
// por valor — a assinatura precisa casar com a do cabeçalho.
#define SURR_STR_PROP(getter, setter, settingsGetter, settingsSetter)         \
    QString SettingsBackend::getter() const { return d->settings.settingsGetter(); } \
    void SettingsBackend::setter(const QString &value)                        \
    {                                                                         \
        if (d->settings.settingsGetter() == value) return;                    \
        d->settings.settingsSetter(value);                                    \
        Q_EMIT changed();                                                     \
    }

#define SURR_VAL_PROP(type, getter, setter, settingsGetter, settingsSetter)   \
    type SettingsBackend::getter() const { return d->settings.settingsGetter(); } \
    void SettingsBackend::setter(type value)                                  \
    {                                                                         \
        if (d->settings.settingsGetter() == value) return;                    \
        d->settings.settingsSetter(value);                                    \
        Q_EMIT changed();                                                     \
    }

SURR_STR_PROP(provider, setProvider, provider, setProvider)
SURR_STR_PROP(groqModel, setGroqModel, groqModel, setGroqModel)
SURR_STR_PROP(language, setLanguage, language, setLanguage)
SURR_STR_PROP(cepifyBaseUrl, setCepifyBaseUrl, cepifyBaseUrl, setCepifyBaseUrl)
SURR_STR_PROP(hotwords, setHotwords, hotwords, setHotwords)
SURR_STR_PROP(inputDeviceId, setInputDeviceId, inputDeviceId, setInputDeviceId)
SURR_VAL_PROP(bool, autoPaste, setAutoPaste, autoPaste, setAutoPaste)
SURR_VAL_PROP(bool, terminalPaste, setTerminalPaste, terminalPasteMode, setTerminalPasteMode)
SURR_VAL_PROP(bool, debugKeepAudio, setDebugKeepAudio, debugKeepAudio, setDebugKeepAudio)
SURR_VAL_PROP(int, minimumHoldMs, setMinimumHoldMs, minimumHoldMs, setMinimumHoldMs)
#undef SURR_STR_PROP
#undef SURR_VAL_PROP

bool SettingsBackend::autostart() const { return d->settings.autostart(); }
void SettingsBackend::setAutostart(bool value)
{
    d->settings.setAutostart(value);
    Q_EMIT changed();
}

bool SettingsBackend::hasGroqKey() const
{
    return groqKeyFromEnvironment() || d->secrets.has(QLatin1String(SecretStore::kGroqKey));
}
bool SettingsBackend::hasCepifyKey() const
{
    return d->secrets.has(QLatin1String(SecretStore::kCepifyKey));
}
bool SettingsBackend::groqKeyFromEnvironment() { return SecretStore::groqKeyComesFromEnvironment(); }
bool SettingsBackend::walletAvailable() const { return !d->secrets.isUsingFallback(); }
QStringList SettingsBackend::inputDevices() const { return d->deviceNames; }
QStringList SettingsBackend::inputDeviceIds() const { return d->deviceIds; }
QString SettingsBackend::statusMessage() const { return d->status; }

void SettingsBackend::setStatus(const QString &message)
{
    d->status = message;
    Q_EMIT statusChanged();
}

void SettingsBackend::setGroqKey(const QString &key)
{
    if (d->secrets.write(QLatin1String(SecretStore::kGroqKey), key.trimmed())) {
        setStatus(QStringLiteral("Chave da Groq salva no KWallet."));
    } else {
        setStatus(QStringLiteral("Não foi possível salvar a chave."));
    }
    Q_EMIT secretsChanged();
}

void SettingsBackend::clearGroqKey()
{
    d->secrets.remove(QLatin1String(SecretStore::kGroqKey));
    setStatus(QStringLiteral("Chave da Groq removida."));
    Q_EMIT secretsChanged();
}

void SettingsBackend::setCepifyKey(const QString &key)
{
    d->secrets.write(QLatin1String(SecretStore::kCepifyKey), key.trimmed());
    setStatus(QStringLiteral("Chave do serviço próprio salva no KWallet."));
    Q_EMIT secretsChanged();
}

void SettingsBackend::clearCepifyKey()
{
    d->secrets.remove(QLatin1String(SecretStore::kCepifyKey));
    Q_EMIT secretsChanged();
}

bool SettingsBackend::legacyKeyAvailable() const
{
    return !SecretStore::findLegacyGroqKey().isEmpty();
}

bool SettingsBackend::importLegacyKey()
{
    const QString legacy = SecretStore::findLegacyGroqKey();
    if (legacy.isEmpty()) {
        setStatus(QStringLiteral("Nenhuma chave encontrada na configuração antiga."));
        return false;
    }
    const bool ok = d->secrets.write(QLatin1String(SecretStore::kGroqKey), legacy);
    setStatus(ok ? QStringLiteral("Chave importada do Sussurro para o KWallet. "
                                  "O arquivo original não foi alterado.")
                 : QStringLiteral("Não foi possível gravar a chave importada."));
    Q_EMIT secretsChanged();
    return ok;
}

void SettingsBackend::authorizePaste()
{
    // O daemon é quem mantém a sessão do portal; pedir daqui abriria uma
    // sessão que morreria junto com esta janela.
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral(SURR_APP_ID), QStringLiteral("/Control"),
        QStringLiteral("br.com.cepify.Surr.Control"), QStringLiteral("authorizePaste"));
    const auto reply = QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
    Q_UNUSED(reply)
    setStatus(QStringLiteral("Pedido enviado — confirme no diálogo do sistema."));
}

void SettingsBackend::configureShortcut()
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral(SURR_APP_ID), QStringLiteral("/Control"),
        QStringLiteral("br.com.cepify.Surr.Control"), QStringLiteral("configureShortcut"));
    QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
    setStatus(QStringLiteral("Abrindo os atalhos do sistema…"));
}

void SettingsBackend::save() { d->settings.save(); }

void SettingsBackend::testProvider()
{
    // Um WAV mínimo e silencioso serve para provar credencial e rede sem
    // gravar a voz do usuário nem gastar áudio de verdade.
    setStatus(QStringLiteral("Testando…"));

    auto *temp = new QTemporaryFile(this);
    temp->setFileTemplate(QDir::tempPath() + QStringLiteral("/surr-teste-XXXXXX.wav"));
    if (!temp->open()) {
        setStatus(QStringLiteral("Não foi possível criar o arquivo de teste."));
        return;
    }
    const QByteArray silence(16000 * 2, '\0'); // 1 s
    temp->write(WavWriter::buildHeader({16000, 1, 16}, silence.size()));
    temp->write(silence);
    temp->flush();

    ITranscriptionProvider *provider = nullptr;
    if (provider_isCepify()) {
        auto *p = new CepifyProvider(&d->nam, this);
        p->setBaseUrl(d->settings.cepifyBaseUrl());
        p->setApiKey(d->secrets.read(QLatin1String(SecretStore::kCepifyKey)));
        provider = p;
    } else {
        auto *p = new GroqProvider(&d->nam, this);
        p->setApiKey(d->secrets.groqApiKey());
        provider = p;
    }

    connect(provider, &ITranscriptionProvider::finished, this,
            [this, provider, temp](const TranscriptionResult &r) {
                setStatus(QStringLiteral("Conexão OK — %1 respondeu em %2 ms.")
                              .arg(r.provider)
                              .arg(r.latency.count()));
                provider->deleteLater();
                temp->deleteLater();
            });
    connect(provider, &ITranscriptionProvider::failed, this,
            [this, provider, temp](const TranscriptionFailure &f) {
                // Silêncio pode voltar como "nada reconhecido": isso ainda
                // prova que credencial e rede estão certas.
                setStatus(f.kind == TranscriptionError::EmptyResult
                              ? QStringLiteral("Conexão OK (o áudio de teste é silencioso).")
                              : f.message);
                provider->deleteLater();
                temp->deleteLater();
            });

    ITranscriptionProvider::Options options;
    options.language = d->settings.language();
    provider->transcribe(temp->fileName(), options);
}

bool SettingsBackend::provider_isCepify() const
{
    return d->settings.provider() == QLatin1String(CepifyProvider::kId);
}

// --------------------------------------------------------------------------

SettingsWindow::SettingsWindow(QObject *parent) : QObject(parent)
{
    m_backend = new SettingsBackend(this);
    m_view = new QQuickView;
    m_view->setTitle(QStringLiteral("Surr — Configurações"));
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->resize(640, 720);
    m_view->setMinimumSize(QSize(520, 560));
    m_view->rootContext()->setContextProperty(QStringLiteral("backend"), m_backend);
    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/surr/qml/Settings.qml")));

    if (m_view->status() != QQuickView::Ready) {
        qCWarning(SURR_UI) << "erro ao carregar as configurações:" << m_view->errors();
    }
}

SettingsWindow::~SettingsWindow() { delete m_view; }

void SettingsWindow::show()
{
    m_view->show();
    m_view->requestActivate();
}

} // namespace surr
