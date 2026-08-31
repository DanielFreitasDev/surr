#pragma once

#include "OpenAiCompatibleProvider.h"

namespace surr {

/**
 * Transcrição pela Groq.
 *
 * Endpoint e modelos conferidos na documentação oficial em 2026-08:
 * POST https://api.groq.com/openai/v1/audio/transcriptions
 * Modelos: whisper-large-v3-turbo (rápido) e whisper-large-v3 (mais preciso).
 */
class GroqProvider : public OpenAiCompatibleProvider
{
    Q_OBJECT
public:
    static constexpr const char *kId = "groq";
    static constexpr const char *kModelTurbo = "whisper-large-v3-turbo";
    static constexpr const char *kModelLarge = "whisper-large-v3";

    explicit GroqProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    qint64 maxUploadBytes() const override;

    /// A conta gratuita aceita 25 MB; a paga, 100 MB.
    void setDeveloperTier(bool enabled);

    static QUrl defaultEndpoint();

protected:
    QString defaultModel() const override;

private:
    bool m_developerTier = false;
};

} // namespace surr
