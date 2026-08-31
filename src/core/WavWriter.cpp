#include "WavWriter.h"

#include <QFile>
#include <QFileDevice>
#include <QtEndian>

#include <limits>

namespace surr {

namespace {
constexpr int kHeaderSize = 44;

void appendLE32(QByteArray &out, quint32 value)
{
    char buf[4];
    qToLittleEndian(value, buf);
    out.append(buf, 4);
}

void appendLE16(QByteArray &out, quint16 value)
{
    char buf[2];
    qToLittleEndian(value, buf);
    out.append(buf, 2);
}
} // namespace

bool WavWriter::Format::isValid() const
{
    // 8 bits não é suportado: o WAV o define como unsigned, e todo o resto
    // do aplicativo trabalha com PCM signed.
    const bool depthOk = bitsPerSample == 16 || bitsPerSample == 24 || bitsPerSample == 32;
    return sampleRate > 0 && channels > 0 && channels <= 8 && depthOk;
}

struct WavWriter::Private {
    QFile file;
    Format format;
    qint64 dataBytes = 0;
    bool finalized = false;
    QString error;
};

WavWriter::WavWriter() : d(std::make_unique<Private>()) {}

WavWriter::~WavWriter()
{
    if (d->file.isOpen() && !d->finalized) {
        finalize();
    }
}

QByteArray WavWriter::buildHeader(const Format &format, quint32 dataSize)
{
    const quint16 channels = static_cast<quint16>(format.channels);
    const quint16 bits = static_cast<quint16>(format.bitsPerSample);
    const quint32 rate = static_cast<quint32>(format.sampleRate);
    const quint16 blockAlign = static_cast<quint16>(channels * (bits / 8));
    const quint32 byteRate = rate * blockAlign;

    QByteArray header;
    header.reserve(kHeaderSize);

    header.append("RIFF", 4);
    // Tamanho do RIFF = tudo depois deste campo = 36 + dados.
    appendLE32(header, 36u + dataSize);
    header.append("WAVE", 4);

    header.append("fmt ", 4);
    appendLE32(header, 16u);        // tamanho do bloco fmt (PCM)
    appendLE16(header, 1);          // WAVE_FORMAT_PCM
    appendLE16(header, channels);
    appendLE32(header, rate);
    appendLE32(header, byteRate);
    appendLE16(header, blockAlign);
    appendLE16(header, bits);

    header.append("data", 4);
    appendLE32(header, dataSize);

    Q_ASSERT(header.size() == kHeaderSize);
    return header;
}

bool WavWriter::open(const QString &path, const Format &format)
{
    if (!format.isValid()) {
        d->error = QStringLiteral("Formato de áudio inválido para WAV.");
        return false;
    }
    if (d->file.isOpen()) {
        d->error = QStringLiteral("O arquivo já está aberto.");
        return false;
    }

    d->file.setFileName(path);
    if (!d->file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        d->error = d->file.errorString();
        return false;
    }

    // O áudio do usuário não deve ser legível por outras contas.
    d->file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    d->format = format;
    d->dataBytes = 0;
    d->finalized = false;
    d->error.clear();

    const QByteArray header = buildHeader(format, 0);
    if (d->file.write(header) != header.size()) {
        d->error = d->file.errorString();
        d->file.close();
        return false;
    }
    return true;
}

bool WavWriter::write(const char *data, qint64 size)
{
    if (!d->file.isOpen() || d->finalized) {
        d->error = QStringLiteral("Escrita em arquivo WAV fechado.");
        return false;
    }
    if (size <= 0) {
        return true;
    }
    if (d->file.write(data, size) != size) {
        d->error = d->file.errorString();
        return false;
    }
    d->dataBytes += size;
    return true;
}

bool WavWriter::finalize()
{
    if (d->finalized) {
        return true;
    }
    if (!d->file.isOpen()) {
        d->error = QStringLiteral("Nada a finalizar.");
        return false;
    }

    d->finalized = true;

    // O WAV usa campos de 32 bits sem sinal; acima de 4 GiB o header não
    // consegue descrever o arquivo. Na prática inatingível para um ditado,
    // mas truncar em silêncio produziria um arquivo corrompido.
    bool ok = true;
    if (d->dataBytes > std::numeric_limits<quint32>::max()) {
        d->error = QStringLiteral("Gravação longa demais para o formato WAV.");
        ok = false;
    } else {
        const QByteArray header = buildHeader(d->format, static_cast<quint32>(d->dataBytes));
        if (!d->file.seek(0) || d->file.write(header) != header.size()) {
            d->error = d->file.errorString();
            ok = false;
        }
    }

    if (!d->file.flush()) {
        d->error = d->file.errorString();
        ok = false;
    }
    d->file.close();
    return ok;
}

void WavWriter::discard()
{
    if (d->file.isOpen()) {
        d->finalized = true;
        d->file.close();
    }
    if (!d->file.fileName().isEmpty()) {
        d->file.remove();
    }
    d->dataBytes = 0;
}

qint64 WavWriter::bytesWritten() const { return d->dataBytes; }

double WavWriter::durationSeconds() const
{
    const int frameSize = d->format.bytesPerFrame();
    if (frameSize <= 0 || d->format.sampleRate <= 0) {
        return 0.0;
    }
    return static_cast<double>(d->dataBytes) / frameSize / d->format.sampleRate;
}

QString WavWriter::path() const { return d->file.fileName(); }
QString WavWriter::errorString() const { return d->error; }
bool WavWriter::isOpen() const { return d->file.isOpen(); }

} // namespace surr
