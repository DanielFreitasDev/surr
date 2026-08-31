#pragma once

#include "TranscriptionResult.h"

#include <QNetworkReply>

class QByteArray;

namespace surr {

/**
 * Traduz falhas de rede/HTTP em frases acionáveis em pt-BR.
 *
 * Uma frase acionável diz o que o usuário pode fazer. "Erro 401" não diz;
 * "A Groq recusou a chave de API" diz. Detalhe técnico vai em `detail`,
 * que só aparece no log de debug.
 */
namespace ProviderErrors {

/// Falha de transporte (sem resposta HTTP útil).
TranscriptionFailure fromNetworkError(QNetworkReply::NetworkError error,
                                      const QString &qtMessage,
                                      const QString &providerName);

/// Falha com código HTTP. @p body é usado para extrair a mensagem do servidor.
TranscriptionFailure fromHttpStatus(int status,
                                    const QByteArray &body,
                                    const QString &providerName,
                                    int retryAfterSeconds = 0);

/// Extrai a mensagem de erro de um corpo JSON no formato OpenAI/Groq.
QString extractServerMessage(const QByteArray &body);

/// True quando repetir a mesma requisição é seguro e pode ajudar.
/// Nunca é true para erros que possam ter sido cobrados.
bool isRetryable(const TranscriptionFailure &failure);

} // namespace ProviderErrors
} // namespace surr
