#include "ProxyResolver.h"

#include <QTest>

using namespace surr;

/**
 * Esta máquina fica atrás de um squid obrigatório, e o autostart do KDE não
 * carrega o `.zshrc`. Se esta classe errar, o aplicativo funciona quando
 * lançado do terminal e falha em silêncio quando lançado pela sessão — que
 * é o modo como ele realmente roda.
 */
class ProxyResolverTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalizesKdeSpaceFormat_data();
    void normalizesKdeSpaceFormat();
    void parsesRealKioslaverc();
    void parsesEnvironmentModeAndDirect();
    void environmentWinsOverKdeConfig();
    void fallsBackToKdeConfigWhenEnvEmpty();
    void honoursExclusions_data();
    void honoursExclusions();
    void directWhenNothingConfigured();

private:
    /// Cópia literal do kioslaverc desta máquina.
    static QString realConfig()
    {
        return QStringLiteral(
            "[Proxy Settings]\n"
            "NoProxyFor=.detran.ce.gov.br,localhost,127.0.0.1,127.0.0.1/32,172.16.0.0/12\n"
            "Proxy Config Script=\n"
            "ProxyType=1\n"
            "ReversedException=false\n"
            "ftpProxy=ftp://127.0.0.1 3128\n"
            "httpProxy=http://127.0.0.1 3128\n"
            "httpsProxy=http://127.0.0.1 3128\n"
            "socksProxy=\n");
    }
};

void ProxyResolverTest::normalizesKdeSpaceFormat_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("expected");

    // O KDE grava o separador como espaço. Nenhum parser de URL aceita isso,
    // e o endereço vira lixo silencioso se não for normalizado.
    QTest::newRow("formato do KDE") << QStringLiteral("http://127.0.0.1 3128")
                                    << QStringLiteral("http://127.0.0.1:3128");
    QTest::newRow("já com dois-pontos") << QStringLiteral("http://127.0.0.1:3128")
                                        << QStringLiteral("http://127.0.0.1:3128");
    QTest::newRow("nome de host") << QStringLiteral("http://proxy.exemplo 8080")
                                  << QStringLiteral("http://proxy.exemplo:8080");
    QTest::newRow("espaços extras") << QStringLiteral("  http://p 3128  ")
                                    << QStringLiteral("http://p:3128");
    QTest::newRow("vazio") << QString() << QString();
}

void ProxyResolverTest::normalizesKdeSpaceFormat()
{
    QFETCH(QString, raw);
    QFETCH(QString, expected);
    QCOMPARE(ProxyResolver::normalizeProxyAddress(raw), expected);
}

void ProxyResolverTest::parsesRealKioslaverc()
{
    const auto config = ProxyResolver::parseKioslaverc(realConfig());
    QVERIFY(config.manual);
    QVERIFY(!config.useEnvironment);
    QCOMPARE(config.httpProxy, QStringLiteral("http://127.0.0.1:3128"));
    QCOMPARE(config.httpsProxy, QStringLiteral("http://127.0.0.1:3128"));
    QCOMPARE(config.noProxyFor.size(), 5);
    QVERIFY(config.noProxyFor.contains(QStringLiteral("localhost")));
}

void ProxyResolverTest::parsesEnvironmentModeAndDirect()
{
    const auto env = ProxyResolver::parseKioslaverc(
        QStringLiteral("[Proxy Settings]\nProxyType=4\n"));
    QVERIFY(env.useEnvironment);
    QVERIFY(!env.manual);

    const auto none = ProxyResolver::parseKioslaverc(
        QStringLiteral("[Proxy Settings]\nProxyType=0\n"));
    QVERIFY(!none.manual);
    QVERIFY(!none.useEnvironment);

    // Chaves fora da seção não podem vazar para dentro dela.
    const auto other = ProxyResolver::parseKioslaverc(
        QStringLiteral("[Outra]\nhttpProxy=http://errado 1\n[Proxy Settings]\nProxyType=1\n"));
    QVERIFY(other.httpProxy.isEmpty());
}

void ProxyResolverTest::environmentWinsOverKdeConfig()
{
    const auto config = ProxyResolver::parseKioslaverc(realConfig());
    const auto proxy = ProxyResolver::resolve(
        QUrl(QStringLiteral("https://api.groq.com/openai/v1/audio/transcriptions")), config,
        QStringLiteral("http://env-proxy:9999"), QStringLiteral("http://env-proxy:9999"),
        QString());

    QCOMPARE(proxy.type(), QNetworkProxy::HttpProxy);
    QCOMPARE(proxy.hostName(), QStringLiteral("env-proxy"));
    QCOMPARE(proxy.port(), quint16(9999));
}

void ProxyResolverTest::fallsBackToKdeConfigWhenEnvEmpty()
{
    // Este é o cenário do autostart: nenhuma variável de ambiente, porque a
    // sessão do KDE não roda o .zshrc.
    const auto config = ProxyResolver::parseKioslaverc(realConfig());
    const auto proxy = ProxyResolver::resolve(
        QUrl(QStringLiteral("https://api.groq.com/openai/v1/audio/transcriptions")), config,
        QString(), QString(), QString());

    QCOMPARE(proxy.type(), QNetworkProxy::HttpProxy);
    QCOMPARE(proxy.hostName(), QStringLiteral("127.0.0.1"));
    QCOMPARE(proxy.port(), quint16(3128));
}

void ProxyResolverTest::honoursExclusions_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<bool>("excluded");

    QTest::newRow("localhost") << QStringLiteral("localhost") << true;
    QTest::newRow("ip local") << QStringLiteral("127.0.0.1") << true;
    QTest::newRow("sufixo de domínio") << QStringLiteral("intra.detran.ce.gov.br") << true;
    QTest::newRow("externo") << QStringLiteral("api.groq.com") << false;
    QTest::newRow("parecido mas não é") << QStringLiteral("detran.ce.gov.br.evil.com") << false;
}

void ProxyResolverTest::honoursExclusions()
{
    QFETCH(QString, host);
    QFETCH(bool, excluded);
    const auto config = ProxyResolver::parseKioslaverc(realConfig());
    QCOMPARE(ProxyResolver::isExcluded(host, config.noProxyFor), excluded);

    const auto proxy = ProxyResolver::resolve(
        QUrl(QStringLiteral("https://%1/x").arg(host)), config, QString(), QString(), QString());
    QCOMPARE(proxy.type() == QNetworkProxy::NoProxy, excluded);
}

void ProxyResolverTest::directWhenNothingConfigured()
{
    const auto proxy = ProxyResolver::resolve(QUrl(QStringLiteral("https://api.groq.com/x")),
                                              {}, QString(), QString(), QString());
    QCOMPARE(proxy.type(), QNetworkProxy::NoProxy);
}

QTEST_MAIN(ProxyResolverTest)
#include "ProxyResolverTest.moc"
