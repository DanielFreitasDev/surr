#pragma once

#include <QLoggingCategory>

/**
 * Categorias de log.
 *
 * Regra de privacidade, válida para todas elas: nunca registrar o texto
 * transcrito, amostras de áudio, chaves de API nem restore tokens. Tamanhos,
 * durações e códigos de erro são o que se pode registrar.
 *
 * Ligar em desenvolvimento com:
 *   QT_LOGGING_RULES="surr.*.debug=true" ./surr
 */
Q_DECLARE_LOGGING_CATEGORY(SURR_APP)
Q_DECLARE_LOGGING_CATEGORY(SURR_AUDIO)
Q_DECLARE_LOGGING_CATEGORY(SURR_NET)
Q_DECLARE_LOGGING_CATEGORY(SURR_PORTAL)
Q_DECLARE_LOGGING_CATEGORY(SURR_SHORTCUT)
Q_DECLARE_LOGGING_CATEGORY(SURR_INJECT)
Q_DECLARE_LOGGING_CATEGORY(SURR_UI)
