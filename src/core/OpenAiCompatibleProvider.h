#pragma once

#include "ITranscriptionProvider.h"

#include <QElapsedTimer>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QHttpMultiPart;
class QFile;
class QTimer;

namespace surr {

/**
 * Base para os serviços com API compatível com a da OpenAI.
 *
 * Groq e o servidor próprio descrito em USO.md falam o mesmo dialeto:
 * multipart POST em `/audio/transcriptions`, `Authorization: Bearer`,
 * resposta `{"text": ...}`. As subclasses só ajustam URL, modelo padrão,
 * limite de upload e parâmetros extras.
 */
class OpenAiCompatibleProvider : public ITranscriptionProvider
{
    Q_OBJECT
public:
    explicit OpenAiCompatibleProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);
    ~OpenAiCompatibleProvider() override;

    void setApiKey(const QString &key);
    bool hasApiKey() const;

    void setEndpoint(const QUrl &url);
    QUrl endpoint() const;

    /// Tempo máximo total da requisição. Padrão 90 s.
    void setTimeoutMs(int ms);

    bool isConfigured() const override;
    bool isBusy() const override;
    void transcribe(const QString &wavPath, const Options &options) override;
    void cancel() override;

protected:
    /// Modelo usado quando `Options::model` vem vazio.
    virtual QString defaultModel() const = 0;
    /// Ganchos para campos extras do multipart (ex.: `hotwords`).
    virtual void addExtraFields(QHttpMultiPart *multiPart, const Options &options);
    /// User-Agent do produto. Nunca pode começar com "OpenAI/": proteções
    /// anti-bot (Cloudflare) casam esse prefixo e devolvem 403.
    virtual QByteArray userAgent() const;

private:
    void handleReply();
    void finishWithFailure(const TranscriptionFailure &failure);
    void cleanup();

    struct Private;
    Private *d;
};

} // namespace surr
