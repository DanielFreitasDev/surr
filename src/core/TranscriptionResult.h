#pragma once

#include <QMetaType>
#include <QString>

#include <chrono>

namespace surr {

/**
 * Resultado normalizado de uma transcrição.
 *
 * Os dois providers devolvem JSON com formatos diferentes; tudo é reduzido
 * a esta struct para que a UI nunca precise saber qual backend respondeu.
 */
struct TranscriptionResult {
    QString text;                       ///< Texto final, já aparado.
    QString provider;                   ///< Identificador do provider ("groq", "cepify").
    QString model;                      ///< Modelo efetivamente usado.
    std::chrono::milliseconds latency{0}; ///< Tempo entre o envio e a resposta.
    qint64 audioBytes = 0;              ///< Tamanho do WAV enviado.

    bool isEmpty() const { return text.isEmpty(); }
};

/**
 * Categorias de falha. A UI escolhe a frase a partir da categoria, e
 * `ProviderErrors` produz a frase padrão em pt-BR.
 */
enum class TranscriptionError {
    None,
    NotConfigured,      ///< Sem chave de API.
    Authentication,     ///< 401/403.
    RateLimited,        ///< 429.
    PayloadTooLarge,    ///< 413.
    BadRequest,         ///< Outros 4xx.
    ServerError,        ///< 5xx.
    Timeout,
    Network,            ///< DNS, recusa de conexão, proxy inalcançável.
    ProxyRefused,       ///< O proxy respondeu e recusou o túnel.
    MalformedResponse,  ///< JSON inválido ou sem o campo de texto.
    EmptyResult,        ///< Transcrição vazia (silêncio).
    Cancelled,
};

struct TranscriptionFailure {
    TranscriptionError kind = TranscriptionError::None;
    QString message;    ///< Frase acionável, pronta para o usuário.
    QString detail;     ///< Detalhe técnico — só para log de debug.
    int httpStatus = 0;
    int retryAfterSeconds = 0; ///< Preenchido quando kind == RateLimited.
};

} // namespace surr

Q_DECLARE_METATYPE(surr::TranscriptionResult)
Q_DECLARE_METATYPE(surr::TranscriptionFailure)
Q_DECLARE_METATYPE(surr::TranscriptionError)
