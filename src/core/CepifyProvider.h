#pragma once

#include "OpenAiCompatibleProvider.h"

namespace surr {

/**
 * Servidor whisper próprio, conforme
 * `projetos/python/whisper-turbo-api/USO.md`.
 *
 * Dialeto compatível com a OpenAI em `<base>/v1/audio/transcriptions`,
 * autenticação `Bearer`, limite de 100 MB e 60 requisições por minuto.
 * Aceita `hotwords` além de `prompt`.
 *
 * Detalhe que o USO.md marca como obrigatório: o `User-Agent` precisa
 * identificar o aplicativo. A proteção anti-bot da Cloudflare na frente do
 * serviço casa qualquer UA começando com `OpenAI/` e devolve 403 antes de a
 * requisição chegar à origem. `OpenAiCompatibleProvider` já envia `surr/<versão>`.
 */
class CepifyProvider : public OpenAiCompatibleProvider
{
    Q_OBJECT
public:
    static constexpr const char *kId = "cepify";
    static constexpr const char *kDefaultModel = "whisper-large-v3-turbo";

    explicit CepifyProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString id() const override;
    QString displayName() const override;
    qint64 maxUploadBytes() const override;

    /**
     * Define a base do serviço. Aceita as formas que um usuário digita:
     * sem esquema, com ou sem `/v1` ao final, com barra sobrando.
     */
    void setBaseUrl(const QString &baseUrl);
    QString baseUrl() const;

    /// URL de verificação de saúde (`GET <base>/health`).
    QUrl healthUrl() const;

    /// Normalização exposta para os testes.
    static QString normalizeBaseUrl(const QString &input);

    void setHotwords(const QString &hotwords);

    static QString defaultBaseUrl();

protected:
    QString defaultModel() const override;
    void addExtraFields(QHttpMultiPart *multiPart, const Options &options) override;

private:
    QString m_baseUrl;
    QString m_hotwords;
};

} // namespace surr
