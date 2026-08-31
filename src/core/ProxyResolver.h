#pragma once

#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QStringList>
#include <QString>
#include <QUrl>

namespace surr {

/**
 * Descobre o proxy a usar, na ordem: ambiente → configuração do KDE.
 *
 * Existe porque o Qt sozinho só lê variáveis de ambiente, e **o autostart do
 * KDE não carrega o `.zshrc`**. Numa rede que exige proxy, o aplicativo
 * funciona quando lançado do terminal e falha quando lançado pela sessão —
 * exatamente o caso desta máquina. Ler o `kioslaverc` fecha essa lacuna.
 *
 * A função de análise é pura para poder ser testada sem rede nem sessão.
 */
class ProxyResolver
{
public:
    struct Config {
        bool useEnvironment = false; ///< kioslaverc ProxyType=4
        bool manual = false;         ///< kioslaverc ProxyType=1
        QString httpProxy;
        QString httpsProxy;
        QStringList noProxyFor;
    };

    /**
     * Interpreta o conteúdo de um `kioslaverc`.
     *
     * O KDE grava o endereço com **espaço no lugar dos dois-pontos**
     * (`http://127.0.0.1 3128`), formato que nenhum parser de URL aceita.
     */
    static Config parseKioslaverc(const QString &contents);

    /// Normaliza `http://host 3128` em `http://host:3128`.
    static QString normalizeProxyAddress(const QString &raw);

    /// True se @p host casa alguma entrada de @p noProxyFor.
    static bool isExcluded(const QString &host, const QStringList &noProxyFor);

    /// Monta o QNetworkProxy para @p url. Vazio = conexão direta.
    static QNetworkProxy resolve(const QUrl &url, const Config &config,
                                 const QString &envHttpProxy,
                                 const QString &envHttpsProxy,
                                 const QString &envNoProxy);

    /// Lê o ambiente e o `kioslaverc` do usuário e aplica em todo o processo.
    static void applyToApplication();

    /// Caminho do kioslaverc do usuário.
    static QString kioslavercPath();
};

} // namespace surr
