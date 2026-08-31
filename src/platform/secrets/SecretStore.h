#pragma once

#include <QObject>
#include <QString>

namespace surr {

/**
 * Guarda credenciais no KWallet.
 *
 * Precedência de leitura, deliberada e documentada no README:
 *
 *   1. variável de ambiente (`GROQ_API_KEY`) — conveniência de desenvolvimento;
 *   2. KWallet — armazenamento normal.
 *
 * O ambiente vence para que dá para testar com outra chave sem mexer na
 * carteira. A variável **nunca** é gravada em disco automaticamente.
 *
 * Nada de chave em KConfig: configuração vai para arquivo em texto puro.
 */
class SecretStore : public QObject
{
    Q_OBJECT
public:
    static constexpr const char *kGroqKey = "groq.apiKey";
    static constexpr const char *kCepifyKey = "cepify.apiKey";
    /// O restore token do portal também é uma credencial: é uma capacidade
    /// de injetar teclas, e mora junto com as chaves.
    static constexpr const char *kRestoreToken = "remoteDesktop.restoreToken";

    explicit SecretStore(QObject *parent = nullptr);
    ~SecretStore() override;

    /// Abre a carteira de forma assíncrona. `readyChanged` avisa o resultado.
    void open();
    bool isOpen() const;
    /// True quando o KWallet não está disponível e usamos o refúgio em disco.
    bool isUsingFallback() const;

    QString read(const QString &key) const;
    bool write(const QString &key, const QString &value);
    bool remove(const QString &key);
    bool has(const QString &key) const;

    /// Chave da Groq considerando a precedência acima.
    QString groqApiKey() const;
    /// True quando a chave em uso veio do ambiente, não da carteira.
    static bool groqKeyComesFromEnvironment();

    /**
     * Chave em texto puro deixada pelo aplicativo anterior em
     * `~/.config/sussurro/config.json`, se houver. Só lê — não apaga nada.
     */
    static QString findLegacyGroqKey();
    static QString legacyConfigPath();

Q_SIGNALS:
    void readyChanged(bool ready);
    /// A carteira não abriu; a interface deve avisar que as chaves não
    /// puderam ser lidas, em vez de fingir que não há chave configurada.
    void unavailable(const QString &reason);

private:
    struct Private;
    Private *d;
};

} // namespace surr
