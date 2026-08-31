#include "SettingsWindow.h"

#include "AudioRecorder.h"
#include "CepifyProvider.h"
#include "GroqProvider.h"
#include "ProxyResolver.h"
#include "SecretStore.h"
#include "Settings.h"
#include "SurrDebug.h"
#include "surr_config.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QTimer>
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

    /**
     * Rascunho da tela. O QML escreve aqui; só o Aplicar leva para o
     * `Settings`.
     *
     * `stored` é a cópia do que está gravado no disco. O "sujo" que acende o
     * botão Aplicar é a comparação entre os dois — nunca uma marca de
     * "mexeu": esbarrar num controle e desfazer tem de apagar o botão de
     * novo, senão ele deixa de servir de alarme.
     *
     * As chaves de API não entram aqui de propósito. Elas têm botão próprio
     * (Salvar/Apagar), vão para o KWallet na hora e não são desfeitas pelo
     * Cancelar.
     */
    struct Draft {
        QString provider;
        QString groqModel;
        QString language;
        QString cepifyBaseUrl;
        QString hotwords;
        QString inputDeviceId;
        bool autoPaste = true;
        bool terminalPaste = false;
        bool autostart = false;
        bool debugKeepAudio = false;
        int minimumHoldMs = 450;
        bool operator==(const Draft &) const = default;
    };
    Draft draft;
    Draft stored;

    Draft fromSettings()
    {
        Draft v;
        v.provider = settings.provider();
        v.groqModel = settings.groqModel();
        v.language = settings.language();
        v.cepifyBaseUrl = settings.cepifyBaseUrl();
        v.hotwords = settings.hotwords();
        v.inputDeviceId = settings.inputDeviceId();
        v.autoPaste = settings.autoPaste();
        v.terminalPaste = settings.terminalPasteMode();
        v.autostart = settings.autostart();
        v.debugKeepAudio = settings.debugKeepAudio();
        v.minimumHoldMs = settings.minimumHoldMs();
        return v;
    }
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

    d->stored = d->fromSettings();
    d->draft = d->stored;
}

SettingsBackend::~SettingsBackend() { delete d; }

// Escrevem no rascunho, nunca no Settings: quem grava é o apply().
// Duas variantes porque QString entra por referência const e os escalares
// por valor — a assinatura precisa casar com a do cabeçalho.
#define SURR_STR_PROP(getter, setter, field)                                  \
    QString SettingsBackend::getter() const { return d->draft.field; }        \
    void SettingsBackend::setter(const QString &value)                        \
    {                                                                         \
        if (d->draft.field == value) return;                                  \
        d->draft.field = value;                                               \
        Q_EMIT changed();                                                     \
        Q_EMIT dirtyChanged();                                                \
    }

#define SURR_VAL_PROP(type, getter, setter, field)                            \
    type SettingsBackend::getter() const { return d->draft.field; }           \
    void SettingsBackend::setter(type value)                                  \
    {                                                                         \
        if (d->draft.field == value) return;                                  \
        d->draft.field = value;                                               \
        Q_EMIT changed();                                                     \
        Q_EMIT dirtyChanged();                                                \
    }

SURR_STR_PROP(provider, setProvider, provider)
SURR_STR_PROP(groqModel, setGroqModel, groqModel)
SURR_STR_PROP(language, setLanguage, language)
SURR_STR_PROP(cepifyBaseUrl, setCepifyBaseUrl, cepifyBaseUrl)
SURR_STR_PROP(hotwords, setHotwords, hotwords)
SURR_STR_PROP(inputDeviceId, setInputDeviceId, inputDeviceId)
SURR_VAL_PROP(bool, autoPaste, setAutoPaste, autoPaste)
SURR_VAL_PROP(bool, terminalPaste, setTerminalPaste, terminalPaste)
SURR_VAL_PROP(bool, autostart, setAutostart, autostart)
SURR_VAL_PROP(bool, debugKeepAudio, setDebugKeepAudio, debugKeepAudio)
SURR_VAL_PROP(int, minimumHoldMs, setMinimumHoldMs, minimumHoldMs)
#undef SURR_STR_PROP
#undef SURR_VAL_PROP

bool SettingsBackend::isDirty() const { return !(d->draft == d->stored); }

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

void SettingsBackend::apply()
{
    if (!isDirty()) {
        return;
    }
    applyDraft();
    // A janela continua aberta: o event loop segue girando e escoa a mensagem
    // sozinho, então aqui não é preciso esperar a resposta.
    notifyDaemon();
}

void SettingsBackend::applyAndClose()
{
    if (!isDirty()) {
        QCoreApplication::quit();
        return;
    }
    applyDraft();

    // Aqui o processo termina logo em seguida, e é isso que muda tudo: uma
    // mensagem D-Bus assíncrona ainda não foi para o socket nesse instante.
    // Medido — com `apply()` seguido de saída imediata, o arquivo era gravado
    // e o daemon não recebia nada. Esperar o watcher é o que garante a
    // entrega, e não é event loop aninhado: é o loop normal continuando a
    // girar até o sinal chegar.
    auto *watcher = new QDBusPendingCallWatcher(notifyDaemon(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                QCoreApplication::quit();
            });
    // Rede de segurança: sem daemon rodando o erro volta rápido, mas se nada
    // voltar a janela não pode ficar presa aberta.
    QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
}

void SettingsBackend::applyDraft()
{
    // Empurra só o que mudou. `setAutostart` escreve (ou apaga) o .desktop do
    // autostart: reescrever esse arquivo por causa de outra opção da mesma
    // tela seria efeito colateral gratuito.
#define SURR_PUSH(field, settingsSetter)                                      \
    if (!(d->draft.field == d->stored.field)) d->settings.settingsSetter(d->draft.field)
    SURR_PUSH(provider, setProvider);
    SURR_PUSH(groqModel, setGroqModel);
    SURR_PUSH(language, setLanguage);
    SURR_PUSH(cepifyBaseUrl, setCepifyBaseUrl);
    SURR_PUSH(hotwords, setHotwords);
    SURR_PUSH(inputDeviceId, setInputDeviceId);
    SURR_PUSH(autoPaste, setAutoPaste);
    SURR_PUSH(terminalPaste, setTerminalPasteMode);
    SURR_PUSH(autostart, setAutostart);
    SURR_PUSH(debugKeepAudio, setDebugKeepAudio);
    SURR_PUSH(minimumHoldMs, setMinimumHoldMs);
#undef SURR_PUSH

    // O writeEntry do KConfig só mexe na cópia em memória. Sem este sync nada
    // chega ao disco — e este processo termina sem destruir o SettingsWindow,
    // então não existe segunda chance no destrutor.
    d->settings.save();

    d->stored = d->draft;
    Q_EMIT dirtyChanged();
    setStatus(QStringLiteral("Configurações aplicadas."));
}

QDBusPendingCall SettingsBackend::notifyDaemon()
{
    // Assíncrono, sem event loop aninhado. Se o daemon não estiver rodando,
    // a chamada volta em erro e não faz falta: ele lê o arquivo na próxima
    // partida.
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral(SURR_APP_ID), QStringLiteral("/Control"),
        QStringLiteral("br.com.cepify.Surr.Control"), QStringLiteral("reloadSettings"));
    return QDBusConnection::sessionBus().asyncCall(message);
}

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
        // Rascunho, não o gravado: o botão testa o que está na tela, senão
        // trocar o endereço e testar mediria o endereço antigo.
        p->setBaseUrl(d->draft.cepifyBaseUrl);
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
    options.language = d->draft.language;
    provider->transcribe(temp->fileName(), options);
}

bool SettingsBackend::provider_isCepify() const
{
    return d->draft.provider == QLatin1String(CepifyProvider::kId);
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

    // O Qt.quit() do QML emite QQmlEngine::quit, e essa ligação com o
    // QCoreApplication só é automática no QQmlApplicationEngine. Aqui a
    // janela é uma QQuickView, que usa um QQmlEngine simples: sem esta
    // conexão, OK e Cancelar não fechariam nada.
    connect(m_view->engine(), &QQmlEngine::quit, qApp, &QCoreApplication::quit);
}

SettingsWindow::~SettingsWindow() { delete m_view; }

void SettingsWindow::show()
{
    m_view->show();
    m_view->requestActivate();
}

} // namespace surr
