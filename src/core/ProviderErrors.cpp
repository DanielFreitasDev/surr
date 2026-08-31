#include "ProviderErrors.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace surr::ProviderErrors {

namespace {

/// O erro de túnel do proxy vem embutido na mensagem do Qt.
/// Distinguir os dois casos importa: "proxy inalcançável" e "o proxy
/// respondeu e recusou o destino" têm ações opostas.
const QRegularExpression &tunnelPattern()
{
    static const QRegularExpression re(QStringLiteral("[Tt]unnel.*?(\\d{3})"));
    return re;
}

} // namespace

QString extractServerMessage(const QByteArray &body)
{
    if (body.isEmpty()) {
        return {};
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // Alguns bloqueios (Cloudflare) devolvem texto puro e curto.
        const QString text = QString::fromUtf8(body).trimmed();
        return text.size() <= 200 ? text : QString();
    }

    const QJsonObject root = doc.object();
    const QJsonValue error = root.value(QStringLiteral("error"));
    if (error.isObject()) {
        const QString message = error.toObject().value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            return message;
        }
    }
    if (error.isString()) {
        return error.toString();
    }
    const QString message = root.value(QStringLiteral("message")).toString();
    if (!message.isEmpty()) {
        return message;
    }
    const QString detail = root.value(QStringLiteral("detail")).toString();
    return detail;
}

TranscriptionFailure fromNetworkError(QNetworkReply::NetworkError error,
                                      const QString &qtMessage,
                                      const QString &providerName)
{
    TranscriptionFailure f;
    f.detail = qtMessage;

    switch (error) {
    case QNetworkReply::OperationCanceledError:
        f.kind = TranscriptionError::Cancelled;
        f.message = QStringLiteral("Transcrição cancelada.");
        return f;

    case QNetworkReply::TimeoutError:
    case QNetworkReply::ProxyTimeoutError:
        f.kind = TranscriptionError::Timeout;
        f.message = QStringLiteral(
            "O serviço de transcrição demorou demais para responder. Tente de novo.");
        return f;

    case QNetworkReply::AuthenticationRequiredError:
        f.kind = TranscriptionError::Authentication;
        f.message = QStringLiteral("%1 recusou a autenticação. Verifique a chave de API.")
                        .arg(providerName);
        return f;

    case QNetworkReply::ProxyConnectionRefusedError:
    case QNetworkReply::ProxyNotFoundError:
        f.kind = TranscriptionError::Network;
        f.message = QStringLiteral(
            "O proxy configurado está inalcançável. Verifique o endereço nas Configurações "
            "de rede do sistema.");
        return f;

    case QNetworkReply::ProxyAuthenticationRequiredError:
        f.kind = TranscriptionError::ProxyRefused;
        f.message = QStringLiteral("O proxy exigiu autenticação e a recusou.");
        return f;

    case QNetworkReply::ProxyConnectionClosedError:
    case QNetworkReply::UnknownProxyError: {
        // O proxy respondeu: o endereço está certo, o túnel é que foi negado.
        const auto match = tunnelPattern().match(qtMessage);
        f.kind = TranscriptionError::ProxyRefused;
        if (match.hasMatch()) {
            f.message = QStringLiteral(
                "O proxy recusou a conexão com o serviço de transcrição (código %1). "
                "Costuma ser passageiro.")
                            .arg(match.captured(1));
        } else {
            f.message = QStringLiteral("O proxy recusou a conexão com o serviço de transcrição.");
        }
        return f;
    }

    case QNetworkReply::HostNotFoundError:
        f.kind = TranscriptionError::Network;
        f.message = QStringLiteral("Não foi possível resolver o endereço do serviço de "
                                   "transcrição. Verifique a conexão.");
        return f;

    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        f.kind = TranscriptionError::Network;
        f.message = QStringLiteral("Não foi possível acessar o serviço de transcrição. "
                                   "Verifique a conexão de rede.");
        return f;

    default:
        f.kind = TranscriptionError::Network;
        f.message = QStringLiteral("Falha de rede ao falar com %1.").arg(providerName);
        return f;
    }
}

TranscriptionFailure fromHttpStatus(int status,
                                    const QByteArray &body,
                                    const QString &providerName,
                                    int retryAfterSeconds)
{
    TranscriptionFailure f;
    f.httpStatus = status;
    f.detail = extractServerMessage(body);

    if (status == 401 || status == 403) {
        f.kind = TranscriptionError::Authentication;
        // 403 com corpo curto, num endpoint atrás de Cloudflare, costuma ser
        // bloqueio de bot por User-Agent — não chave inválida.
        if (status == 403 && f.detail.contains(QStringLiteral("blocked"), Qt::CaseInsensitive)) {
            f.message = QStringLiteral(
                "O servidor bloqueou a requisição antes de chegar ao serviço. "
                "Isso costuma ser proteção anti-bot.");
        } else {
            f.message = QStringLiteral("%1 recusou a autenticação. Verifique a chave de API "
                                       "nas Configurações.")
                            .arg(providerName);
        }
        return f;
    }

    if (status == 413) {
        f.kind = TranscriptionError::PayloadTooLarge;
        f.message = QStringLiteral("A gravação ficou grande demais para o serviço. "
                                   "Faça ditados mais curtos.");
        return f;
    }

    if (status == 429) {
        f.kind = TranscriptionError::RateLimited;
        f.retryAfterSeconds = retryAfterSeconds;
        f.message = retryAfterSeconds > 0
            ? QStringLiteral("Limite de uso atingido. Tente de novo em %1 s.")
                  .arg(retryAfterSeconds)
            : QStringLiteral("Limite de uso do serviço atingido. Aguarde um instante.");
        return f;
    }

    if (status >= 500) {
        f.kind = TranscriptionError::ServerError;
        f.message = QStringLiteral("O serviço de transcrição está instável (erro %1). "
                                   "Tente de novo em instantes.")
                        .arg(status);
        return f;
    }

    if (status >= 400) {
        f.kind = TranscriptionError::BadRequest;
        f.message = f.detail.isEmpty()
            ? QStringLiteral("%1 recusou a requisição (erro %2).").arg(providerName).arg(status)
            : QStringLiteral("%1 recusou a requisição: %2").arg(providerName, f.detail);
        return f;
    }

    f.kind = TranscriptionError::MalformedResponse;
    f.message = QStringLiteral("Resposta inesperada do serviço de transcrição.");
    return f;
}

bool isRetryable(const TranscriptionFailure &failure)
{
    // Um 5xx ou timeout pode ter sido processado e cobrado do outro lado.
    // Repetir automaticamente arriscaria cobrança dupla, então só o
    // transporte claramente não estabelecido é considerado seguro.
    switch (failure.kind) {
    case TranscriptionError::Network:
    case TranscriptionError::ProxyRefused:
        return true;
    default:
        return false;
    }
}

} // namespace surr::ProviderErrors
