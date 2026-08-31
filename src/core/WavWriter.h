#pragma once

#include <QByteArray>
#include <QString>
#include <memory>

class QIODevice;

namespace surr {

/**
 * Escreve um WAV PCM válido de forma incremental.
 *
 * O header RIFF carrega dois campos de tamanho que só se conhecem no fim.
 * `open()` grava um header provisório, `write()` acrescenta amostras e
 * `finalize()` volta ao início e corrige os tamanhos. Se o processo morrer
 * antes do `finalize()`, o arquivo fica com tamanhos zerados — por isso
 * `finalize()` é chamado no caminho de erro também.
 */
class WavWriter
{
public:
    struct Format {
        int sampleRate = 16000;
        int channels = 1;
        int bitsPerSample = 16;

        bool isValid() const;
        int bytesPerFrame() const { return channels * (bitsPerSample / 8); }
    };

    WavWriter();
    ~WavWriter();

    WavWriter(const WavWriter &) = delete;
    WavWriter &operator=(const WavWriter &) = delete;

    /// Cria @p path com permissão 0600 e grava o header provisório.
    bool open(const QString &path, const Format &format);

    /// Acrescenta amostras PCM já no formato declarado em `open()`.
    bool write(const char *data, qint64 size);
    bool write(const QByteArray &data) { return write(data.constData(), data.size()); }

    /// Corrige os tamanhos no header e fecha. Idempotente.
    bool finalize();

    /// Remove o arquivo, fechando antes se preciso.
    void discard();

    qint64 bytesWritten() const;
    double durationSeconds() const;
    QString path() const;
    QString errorString() const;
    bool isOpen() const;

    /// Header de 44 bytes para @p dataSize amostras. Exposto para os testes.
    static QByteArray buildHeader(const Format &format, quint32 dataSize);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace surr
