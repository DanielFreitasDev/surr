#include "CepifyProvider.h"
#include "GroqProvider.h"
#include "ProviderErrors.h"
#include "TranscriptionResult.h"

#include <QNetworkAccessManager>
#include <QTest>

using namespace surr;

class ProviderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void groqDefaults();
    void groqTierChangesUploadLimit();
    void cepifyDefaults();
    void cepifyNormalizesBaseUrl_data();
    void cepifyNormalizesBaseUrl();
    void serverMessageIsExtracted_data();
    void serverMessageIsExtracted();
    void httpStatusMapsToActionableText_data();
    void httpStatusMapsToActionableText();
    void networkErrorsSeparateProxyCauses();
    void billableFailuresAreNeverRetried();

private:
    QNetworkAccessManager nam;
};

void ProviderTest::groqDefaults()
{
    GroqProvider p(&nam);
    QCOMPARE(p.id(), QStringLiteral("groq"));
    QCOMPARE(p.endpoint().toString(),
             QStringLiteral("https://api.groq.com/openai/v1/audio/transcriptions"));
    // Sem chave não há como tentar; o erro tem que ser "não configurado",
    // não uma requisição que volta 401.
    QVERIFY(!p.isConfigured());
    p.setApiKey(QStringLiteral("gsk_exemplo"));
    QVERIFY(p.isConfigured());
}

void ProviderTest::groqTierChangesUploadLimit()
{
    GroqProvider p(&nam);
    QCOMPARE(p.maxUploadBytes(), 25LL * 1024 * 1024);
    p.setDeveloperTier(true);
    QCOMPARE(p.maxUploadBytes(), 100LL * 1024 * 1024);
}

void ProviderTest::cepifyDefaults()
{
    CepifyProvider p(&nam);
    QCOMPARE(p.id(), QStringLiteral("cepify"));
    QCOMPARE(p.endpoint().toString(),
             QStringLiteral("https://whisper.exemplo.com.br/v1/audio/transcriptions"));
    QCOMPARE(p.healthUrl().toString(), QStringLiteral("https://whisper.exemplo.com.br/health"));
    QCOMPARE(p.maxUploadBytes(), 100LL * 1024 * 1024);
}

void ProviderTest::cepifyNormalizesBaseUrl_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    const QString canonical = QStringLiteral("https://whisper.exemplo.com.br");
    QTest::newRow("canônica") << canonical << canonical;
    QTest::newRow("sem esquema") << QStringLiteral("whisper.exemplo.com.br") << canonical;
    QTest::newRow("barra sobrando") << canonical + QStringLiteral("/") << canonical;
    // O USO.md mostra a base com /v1; colar de lá não pode gerar /v1/v1/...
    QTest::newRow("com /v1") << canonical + QStringLiteral("/v1") << canonical;
    QTest::newRow("com /v1/") << canonical + QStringLiteral("/v1/") << canonical;
    QTest::newRow("http explícito") << QStringLiteral("http://192.168.0.5:8000")
                                    << QStringLiteral("http://192.168.0.5:8000");
    QTest::newRow("espaços") << QStringLiteral("  whisper.exemplo.com.br  ") << canonical;
}

void ProviderTest::cepifyNormalizesBaseUrl()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(CepifyProvider::normalizeBaseUrl(input), expected);

    CepifyProvider p(&nam);
    p.setBaseUrl(input);
    QCOMPARE(p.endpoint().toString(), expected + QStringLiteral("/v1/audio/transcriptions"));
}

void ProviderTest::serverMessageIsExtracted_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("expected");

    QTest::newRow("formato openai")
        << QByteArrayLiteral(R"({"error":{"message":"Invalid API Key","type":"invalid_request"}})")
        << QStringLiteral("Invalid API Key");
    QTest::newRow("error string")
        << QByteArrayLiteral(R"({"error":"boom"})") << QStringLiteral("boom");
    QTest::newRow("message na raiz")
        << QByteArrayLiteral(R"({"message":"nope"})") << QStringLiteral("nope");
    QTest::newRow("detail (FastAPI)")
        << QByteArrayLiteral(R"({"detail":"chave ausente"})") << QStringLiteral("chave ausente");
    // A Cloudflare bloqueia com texto puro curto, não JSON.
    QTest::newRow("texto puro da cloudflare")
        << QByteArrayLiteral("Your request was blocked")
        << QStringLiteral("Your request was blocked");
    QTest::newRow("vazio") << QByteArray() << QString();
}

void ProviderTest::serverMessageIsExtracted()
{
    QFETCH(QByteArray, body);
    QFETCH(QString, expected);
    QCOMPARE(ProviderErrors::extractServerMessage(body), expected);
}

void ProviderTest::httpStatusMapsToActionableText_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<TranscriptionError>("kind");

    QTest::newRow("401") << 401 << TranscriptionError::Authentication;
    QTest::newRow("403") << 403 << TranscriptionError::Authentication;
    QTest::newRow("413") << 413 << TranscriptionError::PayloadTooLarge;
    QTest::newRow("429") << 429 << TranscriptionError::RateLimited;
    QTest::newRow("400") << 400 << TranscriptionError::BadRequest;
    QTest::newRow("500") << 500 << TranscriptionError::ServerError;
    QTest::newRow("503") << 503 << TranscriptionError::ServerError;
}

void ProviderTest::httpStatusMapsToActionableText()
{
    QFETCH(int, status);
    QFETCH(TranscriptionError, kind);

    const auto f = ProviderErrors::fromHttpStatus(status, {}, QStringLiteral("Groq"));
    QCOMPARE(f.kind, kind);
    QCOMPARE(f.httpStatus, status);
    // Uma frase acionável nunca é só o número.
    QVERIFY(!f.message.isEmpty());
    QVERIFY(f.message.size() > 10);

    if (status == 429) {
        const auto withRetry =
            ProviderErrors::fromHttpStatus(429, {}, QStringLiteral("Groq"), 30);
        QCOMPARE(withRetry.retryAfterSeconds, 30);
        QVERIFY(withRetry.message.contains(QStringLiteral("30")));
    }
}

void ProviderTest::networkErrorsSeparateProxyCauses()
{
    // Esta máquina fica atrás de um squid obrigatório. As duas falhas de
    // proxy pedem ações opostas, então não podem virar a mesma frase:
    // "proxy inalcançável" manda conferir o endereço; "o proxy recusou o
    // túnel" significa que o endereço está certo.
    const auto unreachable = ProviderErrors::fromNetworkError(
        QNetworkReply::ProxyConnectionRefusedError, QStringLiteral("refused"),
        QStringLiteral("Groq"));
    QCOMPARE(unreachable.kind, TranscriptionError::Network);
    QVERIFY(unreachable.message.contains(QStringLiteral("proxy"), Qt::CaseInsensitive));

    const auto refusedTunnel = ProviderErrors::fromNetworkError(
        QNetworkReply::UnknownProxyError,
        QStringLiteral("Tunnel connection failed: 503 Service Unavailable"),
        QStringLiteral("Groq"));
    QCOMPARE(refusedTunnel.kind, TranscriptionError::ProxyRefused);
    QVERIFY(refusedTunnel.message.contains(QStringLiteral("503")));
    QVERIFY(unreachable.message != refusedTunnel.message);

    const auto timeout = ProviderErrors::fromNetworkError(
        QNetworkReply::TimeoutError, {}, QStringLiteral("Groq"));
    QCOMPARE(timeout.kind, TranscriptionError::Timeout);

    const auto cancelled = ProviderErrors::fromNetworkError(
        QNetworkReply::OperationCanceledError, {}, QStringLiteral("Groq"));
    QCOMPARE(cancelled.kind, TranscriptionError::Cancelled);
}

void ProviderTest::billableFailuresAreNeverRetried()
{
    // Um 5xx ou um timeout podem ter sido processados e cobrados do outro
    // lado. Repetir sozinho arriscaria cobrar duas vezes pelo mesmo ditado.
    for (auto kind : {TranscriptionError::ServerError, TranscriptionError::Timeout,
                      TranscriptionError::RateLimited, TranscriptionError::Authentication,
                      TranscriptionError::PayloadTooLarge}) {
        TranscriptionFailure f;
        f.kind = kind;
        QVERIFY2(!ProviderErrors::isRetryable(f), "falha cobrável não pode ser repetida sozinha");
    }

    TranscriptionFailure network;
    network.kind = TranscriptionError::Network;
    QVERIFY(ProviderErrors::isRetryable(network));
}

QTEST_MAIN(ProviderTest)
#include "ProviderTest.moc"
