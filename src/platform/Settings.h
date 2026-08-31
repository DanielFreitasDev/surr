#pragma once

#include <QObject>
#include <QString>

namespace surr {

/**
 * Configuração não secreta, em KConfig (`~/.config/surrrc`).
 *
 * Chaves de API e o restore token do portal **não** passam por aqui: KConfig
 * grava texto puro. Isso é do `SecretStore`.
 */
class Settings : public QObject
{
    Q_OBJECT
public:
    explicit Settings(QObject *parent = nullptr);
    ~Settings() override;

    // --- Transcrição ---
    QString provider() const;              ///< "groq" ou "cepify"
    void setProvider(const QString &id);

    QString groqModel() const;             ///< whisper-large-v3-turbo | whisper-large-v3
    void setGroqModel(const QString &model);

    /// ISO-639-1 ("pt"); vazio = detecção automática.
    QString language() const;
    void setLanguage(const QString &code);

    QString cepifyBaseUrl() const;
    void setCepifyBaseUrl(const QString &url);

    QString hotwords() const;
    void setHotwords(const QString &value);

    // --- Áudio ---
    QString inputDeviceId() const;
    void setInputDeviceId(const QString &id);

    int minimumHoldMs() const;
    void setMinimumHoldMs(int ms);

    int maximumSeconds() const;
    void setMaximumSeconds(int seconds);

    // --- Atalho ---
    QString preferredTrigger() const;
    void setPreferredTrigger(const QString &trigger);

    // --- Entrega ---
    bool autoPaste() const;
    void setAutoPaste(bool enabled);

    /// Ctrl+Shift+V em vez de Ctrl+V (colagem em terminais).
    bool terminalPasteMode() const;
    void setTerminalPasteMode(bool enabled);

    int pasteDelayMs() const;
    void setPasteDelayMs(int ms);

    // --- Aplicativo ---
    bool onboardingDone() const;
    void setOnboardingDone(bool done);

    bool debugKeepAudio() const;
    void setDebugKeepAudio(bool keep);

    /// Autostart XDG (`~/.config/autostart/<app-id>.desktop`).
    bool autostart() const;
    void setAutostart(bool enabled);

    void save();

    /// Relê o arquivo do disco, descartando o que estava em memória.
    ///
    /// O KConfig não percebe sozinho que outro processo gravou. O daemon
    /// precisa disto quando a janela de configurações aplica; sem o reparse,
    /// ele seguiria com os valores lidos na partida até o próximo boot.
    void reload();

Q_SIGNALS:
    void changed();

private:
    struct Private;
    Private *d;
};

} // namespace surr
