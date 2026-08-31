#include "SurrDebug.h"

// Tudo fica em QtInfoMsg por padrão: um aplicativo que vive aberto o dia
// inteiro não deve encher o journal com detalhe de depuração.
Q_LOGGING_CATEGORY(SURR_APP, "surr.app", QtInfoMsg)
Q_LOGGING_CATEGORY(SURR_AUDIO, "surr.audio", QtInfoMsg)
Q_LOGGING_CATEGORY(SURR_NET, "surr.net", QtInfoMsg)
Q_LOGGING_CATEGORY(SURR_PORTAL, "surr.portal", QtInfoMsg)
Q_LOGGING_CATEGORY(SURR_SHORTCUT, "surr.shortcut", QtInfoMsg)
Q_LOGGING_CATEGORY(SURR_INJECT, "surr.inject", QtInfoMsg)
Q_LOGGING_CATEGORY(SURR_UI, "surr.ui", QtInfoMsg)
