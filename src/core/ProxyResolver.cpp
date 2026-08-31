#include "ProxyResolver.h"

#include "SurrDebug.h"

#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

namespace surr {

QString ProxyResolver::normalizeProxyAddress(const QString &raw)
{
    QString value = raw.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    // "http://127.0.0.1 3128" → "http://127.0.0.1:3128"
    static const QRegularExpression spacePort(QStringLiteral("\\s+(\\d{1,5})\\s*$"));
    const auto match = spacePort.match(value);
    if (match.hasMatch()) {
        value = value.left(match.capturedStart()) + QLatin1Char(':') + match.captured(1);
    }
    return value;
}

ProxyResolver::Config ProxyResolver::parseKioslaverc(const QString &contents)
{
    Config config;
    bool inSection = false;

    const auto lines = contents.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inSection = (line.compare(QStringLiteral("[Proxy Settings]"), Qt::CaseInsensitive) == 0);
            continue;
        }
        if (!inSection || line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        if (key.compare(QStringLiteral("ProxyType"), Qt::CaseInsensitive) == 0) {
            // 1 = manual, 4 = usar variáveis de ambiente.
            config.manual = (value == QLatin1String("1"));
            config.useEnvironment = (value == QLatin1String("4"));
        } else if (key.compare(QStringLiteral("httpProxy"), Qt::CaseInsensitive) == 0) {
            config.httpProxy = normalizeProxyAddress(value);
        } else if (key.compare(QStringLiteral("httpsProxy"), Qt::CaseInsensitive) == 0) {
            config.httpsProxy = normalizeProxyAddress(value);
        } else if (key.compare(QStringLiteral("NoProxyFor"), Qt::CaseInsensitive) == 0) {
            config.noProxyFor = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (QString &entry : config.noProxyFor) {
                entry = entry.trimmed();
            }
        }
    }
    return config;
}

bool ProxyResolver::isExcluded(const QString &host, const QStringList &noProxyFor)
{
    for (const QString &raw : noProxyFor) {
        const QString entry = raw.trimmed();
        if (entry.isEmpty()) {
            continue;
        }
        if (host.compare(entry, Qt::CaseInsensitive) == 0) {
            return true;
        }
        // ".exemplo.com" cobre "algo.exemplo.com".
        if (entry.startsWith(QLatin1Char('.')) && host.endsWith(entry, Qt::CaseInsensitive)) {
            return true;
        }
        // Entradas com máscara (172.16.0.0/12) não são expandidas aqui;
        // basta o prefixo literal, que cobre o uso real do kioslaverc.
        if (entry.contains(QLatin1Char('/'))) {
            const QString prefix = entry.section(QLatin1Char('/'), 0, 0);
            if (host == prefix) {
                return true;
            }
        }
    }
    return false;
}

QNetworkProxy ProxyResolver::resolve(const QUrl &url, const Config &config,
                                     const QString &envHttpProxy,
                                     const QString &envHttpsProxy,
                                     const QString &envNoProxy)
{
    const QString host = url.host();
    const bool secure = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;

    // O ambiente tem precedência: quem exportou http_proxy quis aquilo.
    QString address = secure ? envHttpsProxy : envHttpProxy;
    QStringList exclusions = envNoProxy.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &e : exclusions) {
        e = e.trimmed();
    }

    if (address.isEmpty() && config.manual) {
        address = secure ? config.httpsProxy : config.httpProxy;
        if (address.isEmpty()) {
            address = config.httpProxy; // muitos setups só definem o http
        }
        exclusions = config.noProxyFor;
    }

    if (address.isEmpty() || isExcluded(host, exclusions)) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }

    const QUrl proxyUrl(normalizeProxyAddress(address));
    if (!proxyUrl.isValid() || proxyUrl.host().isEmpty()) {
        return QNetworkProxy(QNetworkProxy::NoProxy);
    }

    QNetworkProxy proxy(QNetworkProxy::HttpProxy, proxyUrl.host(),
                        static_cast<quint16>(proxyUrl.port(3128)));
    if (!proxyUrl.userName().isEmpty()) {
        proxy.setUser(proxyUrl.userName());
        proxy.setPassword(proxyUrl.password());
    }
    return proxy;
}

QString ProxyResolver::kioslavercPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/kioslaverc");
}

void ProxyResolver::applyToApplication()
{
    Config config;
    QFile file(kioslavercPath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        config = parseKioslaverc(QString::fromUtf8(file.readAll()));
    }

    const QString envHttp = qEnvironmentVariable("http_proxy",
                                                 qEnvironmentVariable("HTTP_PROXY"));
    const QString envHttps = qEnvironmentVariable("https_proxy",
                                                  qEnvironmentVariable("HTTPS_PROXY"));
    const QString envNo = qEnvironmentVariable("no_proxy", qEnvironmentVariable("NO_PROXY"));

    // Uma consulta de exemplo só para registrar o que foi decidido; o
    // QNetworkProxyFactory faz a escolha por requisição.
    class Factory : public QNetworkProxyFactory
    {
    public:
        Factory(Config c, QString h, QString s, QString n)
            : m_config(std::move(c)), m_http(std::move(h)), m_https(std::move(s)),
              m_no(std::move(n)) {}

        QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &query) override
        {
            return {ProxyResolver::resolve(query.url(), m_config, m_http, m_https, m_no)};
        }

    private:
        Config m_config;
        QString m_http, m_https, m_no;
    };

    QNetworkProxyFactory::setApplicationProxyFactory(
        new Factory(config, envHttp, envHttps, envNo));

    const bool viaEnv = !envHttp.isEmpty() || !envHttps.isEmpty();
    qCInfo(SURR_NET) << "proxy:" << (viaEnv ? "do ambiente"
                                    : config.manual ? "do kioslaverc"
                                                    : "conexão direta");
}

} // namespace surr
