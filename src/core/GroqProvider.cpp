#include "GroqProvider.h"

namespace surr {

GroqProvider::GroqProvider(QNetworkAccessManager *nam, QObject *parent)
    : OpenAiCompatibleProvider(nam, parent)
{
    setEndpoint(defaultEndpoint());
}

QUrl GroqProvider::defaultEndpoint()
{
    return QUrl(QStringLiteral("https://api.groq.com/openai/v1/audio/transcriptions"));
}

QString GroqProvider::id() const { return QString::fromLatin1(kId); }
QString GroqProvider::displayName() const { return QStringLiteral("Groq"); }
QString GroqProvider::defaultModel() const { return QString::fromLatin1(kModelTurbo); }

void GroqProvider::setDeveloperTier(bool enabled) { m_developerTier = enabled; }

qint64 GroqProvider::maxUploadBytes() const
{
    return m_developerTier ? 100LL * 1024 * 1024 : 25LL * 1024 * 1024;
}

} // namespace surr
