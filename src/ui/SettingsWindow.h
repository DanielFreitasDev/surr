#pragma once

#include <QDBusPendingCall>
#include <QObject>
#include <QStringList>

class QQuickView;

namespace surr {

class Settings;
class SecretStore;

/**
 * Objeto exposto ao QML das configurações.
 *
 * Roda no processo `--settings`, que **não** liga o layer-shell — por isso
 * aqui a janela é uma janela normal, com decoração e barra de tarefas.
 */
class SettingsBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY changed)
    Q_PROPERTY(QString groqModel READ groqModel WRITE setGroqModel NOTIFY changed)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(QString cepifyBaseUrl READ cepifyBaseUrl WRITE setCepifyBaseUrl NOTIFY changed)
    Q_PROPERTY(QString hotwords READ hotwords WRITE setHotwords NOTIFY changed)
    Q_PROPERTY(QString inputDeviceId READ inputDeviceId WRITE setInputDeviceId NOTIFY changed)
    Q_PROPERTY(bool autoPaste READ autoPaste WRITE setAutoPaste NOTIFY changed)
    Q_PROPERTY(bool terminalPaste READ terminalPaste WRITE setTerminalPaste NOTIFY changed)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY changed)
    Q_PROPERTY(bool debugKeepAudio READ debugKeepAudio WRITE setDebugKeepAudio NOTIFY changed)
    Q_PROPERTY(int minimumHoldMs READ minimumHoldMs WRITE setMinimumHoldMs NOTIFY changed)
    Q_PROPERTY(bool hasGroqKey READ hasGroqKey NOTIFY secretsChanged)
    Q_PROPERTY(bool hasCepifyKey READ hasCepifyKey NOTIFY secretsChanged)
    Q_PROPERTY(bool groqKeyFromEnvironment READ groqKeyFromEnvironment CONSTANT)
    Q_PROPERTY(bool walletAvailable READ walletAvailable NOTIFY secretsChanged)
    Q_PROPERTY(QStringList inputDevices READ inputDevices CONSTANT)
    Q_PROPERTY(QStringList inputDeviceIds READ inputDeviceIds CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    /// Verdadeiro quando o rascunho difere do que está gravado. É o que
    /// acende o botão Aplicar — e o que o faz apagar sozinho quando o valor
    /// volta ao original, que é o ponto de esbarrar sem querer num controle.
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)

public:
    explicit SettingsBackend(QObject *parent = nullptr);
    ~SettingsBackend() override;

    QString provider() const;
    void setProvider(const QString &value);
    QString groqModel() const;
    void setGroqModel(const QString &value);
    QString language() const;
    void setLanguage(const QString &value);
    QString cepifyBaseUrl() const;
    void setCepifyBaseUrl(const QString &value);
    QString hotwords() const;
    void setHotwords(const QString &value);
    QString inputDeviceId() const;
    void setInputDeviceId(const QString &value);
    bool autoPaste() const;
    void setAutoPaste(bool value);
    bool terminalPaste() const;
    void setTerminalPaste(bool value);
    bool autostart() const;
    void setAutostart(bool value);
    bool debugKeepAudio() const;
    void setDebugKeepAudio(bool value);
    int minimumHoldMs() const;
    void setMinimumHoldMs(int value);

    bool hasGroqKey() const;
    bool hasCepifyKey() const;
    static bool groqKeyFromEnvironment();
    bool walletAvailable() const;
    QStringList inputDevices() const;
    QStringList inputDeviceIds() const;
    QString statusMessage() const;
    bool isDirty() const;

    Q_INVOKABLE void setGroqKey(const QString &key);
    Q_INVOKABLE void clearGroqKey();
    Q_INVOKABLE void setCepifyKey(const QString &key);
    Q_INVOKABLE void clearCepifyKey();
    /// Importa a chave em texto puro deixada pelo aplicativo anterior.
    Q_INVOKABLE bool importLegacyKey();
    Q_INVOKABLE bool legacyKeyAvailable() const;
    /// Pede ao daemon que abra o diálogo de autorização do portal.
    Q_INVOKABLE void authorizePaste();
    /// Abre a tela do sistema para trocar o atalho.
    Q_INVOKABLE void configureShortcut();
    /// Grava o rascunho, sincroniza o arquivo e avisa o daemon (botão Aplicar).
    Q_INVOKABLE void apply();
    /// Igual ao apply, e fecha a janela quando o daemon confirmar (botão OK).
    Q_INVOKABLE void applyAndClose();
    Q_INVOKABLE void testProvider();

Q_SIGNALS:
    void changed();
    void secretsChanged();
    void statusChanged();
    void dirtyChanged();

private:
    void setStatus(const QString &message);
    bool provider_isCepify() const;
    /// Empurra o rascunho para o `Settings` e sincroniza o arquivo.
    void applyDraft();
    /// Pede ao daemon que releia o `surrrc`. Sem isto, o que foi aplicado só
    /// valeria no próximo boot: o KConfig dele não percebe a gravação.
    QDBusPendingCall notifyDaemon();

    struct Private;
    Private *d;
};

/// A janela em si: um QQuickView com a interface QML.
class SettingsWindow : public QObject
{
    Q_OBJECT
public:
    explicit SettingsWindow(QObject *parent = nullptr);
    ~SettingsWindow() override;
    void show();

private:
    QQuickView *m_view = nullptr;
    SettingsBackend *m_backend = nullptr;
};

} // namespace surr
