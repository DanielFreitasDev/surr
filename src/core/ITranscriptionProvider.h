#pragma once

#include "TranscriptionResult.h"

#include <QObject>
#include <QString>

namespace surr {

/**
 * Contrato comum aos backends de transcrição.
 *
 * Uma transcrição por vez, por instância. `transcribe()` é assíncrono: o
 * resultado chega por `finished()` ou `failed()`, nunca pelos dois.
 */
class ITranscriptionProvider : public QObject
{
    Q_OBJECT
public:
    struct Options {
        QString language;   ///< ISO-639-1 ("pt"); vazio = detecção automática.
        QString prompt;     ///< Contexto opcional para o modelo.
        QString model;
        double temperature = 0.0;
        bool verbose = false; ///< response_format=verbose_json (modo debug).
    };

    using QObject::QObject;
    ~ITranscriptionProvider() override = default;

    /// Identificador estável, usado em configuração e em TranscriptionResult.
    virtual QString id() const = 0;
    /// Nome exibido ao usuário.
    virtual QString displayName() const = 0;

    /// True quando há credenciais suficientes para tentar uma transcrição.
    virtual bool isConfigured() const = 0;

    /// Limite de upload aceito pelo serviço, em bytes.
    virtual qint64 maxUploadBytes() const = 0;

    /**
     * Envia @p wavPath para transcrição.
     * Falha imediatamente (via `failed()`) se já houver uma em andamento.
     */
    virtual void transcribe(const QString &wavPath, const Options &options) = 0;

    /// Cancela a requisição em andamento, se houver. Emite `failed(Cancelled)`.
    virtual void cancel() = 0;

    virtual bool isBusy() const = 0;

Q_SIGNALS:
    void finished(const surr::TranscriptionResult &result);
    void failed(const surr::TranscriptionFailure &failure);
};

} // namespace surr
