#include "CepifyProvider.h"

#include <QHttpMultiPart>
#include <QHttpPart>

namespace surr {

namespace {
QHttpPart makeTextPart(const QString &name, const QString &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QStringLiteral("form-data; name=\"%1\"").arg(name)));
    part.setBody(value.toUtf8());
    return part;
}
} // namespace

CepifyProvider::CepifyProvider(QNetworkAccessManager *nam, QObject *parent)
    : OpenAiCompatibleProvider(nam, parent)
{
    setBaseUrl(defaultBaseUrl());
}

QString CepifyProvider::defaultBaseUrl()
{
    return QStringLiteral("https://whisper.exemplo.com.br");
}

QString CepifyProvider::normalizeBaseUrl(const QString &input)
{
    QString value = input.trimmed();
    if (value.isEmpty()) {
        return {};
    }

    // Quem digita o endereço raramente digita o esquema.
    if (!value.contains(QStringLiteral("://"))) {
        value.prepend(QStringLiteral("https://"));
    }

    while (value.endsWith(QLatin1Char('/'))) {
        value.chop(1);
    }

    // O USO.md mostra a base com `/v1`; o caminho completo é montado aqui,
    // então um `/v1` colado pelo usuário viraria `/v1/v1/...`.
    if (value.endsWith(QStringLiteral("/v1"))) {
        value.chop(3);
    }

    while (value.endsWith(QLatin1Char('/'))) {
        value.chop(1);
    }
    return value;
}

void CepifyProvider::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = normalizeBaseUrl(baseUrl);
    setEndpoint(m_baseUrl.isEmpty()
                    ? QUrl()
                    : QUrl(m_baseUrl + QStringLiteral("/v1/audio/transcriptions")));
}

QString CepifyProvider::baseUrl() const { return m_baseUrl; }

QUrl CepifyProvider::healthUrl() const
{
    return m_baseUrl.isEmpty() ? QUrl() : QUrl(m_baseUrl + QStringLiteral("/health"));
}

QString CepifyProvider::id() const { return QString::fromLatin1(kId); }

QString CepifyProvider::displayName() const { return QStringLiteral("Whisper próprio"); }

QString CepifyProvider::defaultModel() const { return QString::fromLatin1(kDefaultModel); }

qint64 CepifyProvider::maxUploadBytes() const { return 100LL * 1024 * 1024; }

void CepifyProvider::setHotwords(const QString &hotwords) { m_hotwords = hotwords.trimmed(); }

void CepifyProvider::addExtraFields(QHttpMultiPart *multiPart, const Options &)
{
    // `hotwords` ensina vocabulário de domínio ao modelo; é uma extensão
    // deste servidor, não existe na Groq.
    if (!m_hotwords.isEmpty()) {
        multiPart->append(makeTextPart(QStringLiteral("hotwords"), m_hotwords));
    }
}

} // namespace surr
