#pragma once

#include <QString>

namespace surr::PortalPaths {

/**
 * Caminho do objeto Request que o portal vai criar para uma chamada.
 *
 * Precisa ser previsto pelo cliente porque o sinal `Response` pode ser
 * emitido antes de a chamada retornar. Quem só conecta depois de receber o
 * handle perde o sinal — e o D-Bus não guarda sinais para regras de match
 * criadas mais tarde, então o aplicativo espera para sempre.
 *
 * Formato: /org/freedesktop/portal/desktop/request/<SENDER>/<TOKEN>
 * onde SENDER é o nome único do barramento sem o ':' inicial e com os '.'
 * trocados por '_'  (":1.234" → "1_234").
 */
QString requestPath(const QString &uniqueBusName, const QString &handleToken);

/// Mesma transformação de nome, exposta para teste.
QString sanitizeBusName(const QString &uniqueBusName);

/// Gera um token único e válido como elemento de object path.
QString makeToken(const char *prefix);

} // namespace surr::PortalPaths
