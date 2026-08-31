#include "WavWriter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

using namespace surr;

/// Um WAV com header errado é aceito pelo disco e recusado pela API, o que
/// aparece como "erro de transcrição" e manda procurar no lugar errado.
class WavWriterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void headerIsCanonical();
    void headerCarriesFormat_data();
    void headerCarriesFormat();
    void sizesAreFixedOnFinalize();
    void durationMatchesSampleCount();
    void fileIsPrivate();
    void discardRemovesFile();
    void rejectsInvalidFormat();
    void finalizeIsIdempotent();

private:
    static quint32 le32(const QByteArray &b, int offset)
    {
        return qFromLittleEndian<quint32>(b.constData() + offset);
    }
    static quint16 le16(const QByteArray &b, int offset)
    {
        return qFromLittleEndian<quint16>(b.constData() + offset);
    }
};

void WavWriterTest::headerIsCanonical()
{
    WavWriter::Format fmt{16000, 1, 16};
    const QByteArray h = WavWriter::buildHeader(fmt, 0);

    QCOMPARE(h.size(), 44);
    QCOMPARE(h.mid(0, 4), QByteArrayLiteral("RIFF"));
    QCOMPARE(h.mid(8, 4), QByteArrayLiteral("WAVE"));
    QCOMPARE(h.mid(12, 4), QByteArrayLiteral("fmt "));
    QCOMPARE(h.mid(36, 4), QByteArrayLiteral("data"));
    QCOMPARE(le32(h, 16), 16u); // bloco fmt PCM
    QCOMPARE(le16(h, 20), quint16(1)); // WAVE_FORMAT_PCM
}

void WavWriterTest::headerCarriesFormat_data()
{
    QTest::addColumn<int>("rate");
    QTest::addColumn<int>("channels");
    QTest::addColumn<int>("bits");

    // Os formatos que os microfones desta máquina realmente expõem, além do
    // alvo de 16 kHz mono: nenhum dispositivo daqui oferece o alvo nativamente.
    QTest::newRow("alvo 16k mono s16") << 16000 << 1 << 16;
    QTest::newRow("webcam 32k estéreo") << 32000 << 2 << 16;
    QTest::newRow("usb 48k estéreo") << 48000 << 2 << 16;
    QTest::newRow("interno 48k s32") << 48000 << 2 << 32;
}

void WavWriterTest::headerCarriesFormat()
{
    QFETCH(int, rate);
    QFETCH(int, channels);
    QFETCH(int, bits);

    const WavWriter::Format fmt{rate, channels, bits};
    const QByteArray h = WavWriter::buildHeader(fmt, 1000);

    QCOMPARE(le16(h, 22), quint16(channels));
    QCOMPARE(le32(h, 24), quint32(rate));
    const quint16 blockAlign = quint16(channels * (bits / 8));
    QCOMPARE(le16(h, 32), blockAlign);
    QCOMPARE(le16(h, 34), quint16(bits));
    QCOMPARE(le32(h, 28), quint32(rate) * blockAlign); // byte rate
    QCOMPARE(le32(h, 40), 1000u);                      // tamanho dos dados
    QCOMPARE(le32(h, 4), 36u + 1000u);                 // tamanho do RIFF
}

void WavWriterTest::sizesAreFixedOnFinalize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("a.wav"));

    WavWriter w;
    QVERIFY(w.open(path, {16000, 1, 16}));

    const QByteArray samples(3200, '\0'); // 0,1 s a 16 kHz mono s16
    QVERIFY(w.write(samples));
    QVERIFY(w.write(samples));
    QVERIFY(w.finalize());

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray content = f.readAll();

    QCOMPARE(content.size(), 44 + 6400);
    // Este é o ponto: o header foi escrito com zero e precisa ter voltado
    // atrás para registrar o tamanho real.
    QCOMPARE(le32(content, 40), 6400u);
    QCOMPARE(le32(content, 4), 36u + 6400u);
}

void WavWriterTest::durationMatchesSampleCount()
{
    QTemporaryDir dir;
    WavWriter w;
    QVERIFY(w.open(dir.filePath(QStringLiteral("b.wav")), {16000, 1, 16}));
    w.write(QByteArray(32000, '\0')); // 1 s
    QCOMPARE(w.bytesWritten(), 32000);
    QVERIFY(qAbs(w.durationSeconds() - 1.0) < 1e-9);
    w.finalize();
}

void WavWriterTest::fileIsPrivate()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("c.wav"));
    WavWriter w;
    QVERIFY(w.open(path, {16000, 1, 16}));
    w.finalize();

    // A gravação é a voz do usuário; outras contas da máquina não devem lê-la.
    const auto perms = QFile(path).permissions();
    QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
    QVERIFY(!perms.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!perms.testFlag(QFileDevice::ReadOther));
}

void WavWriterTest::discardRemovesFile()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("d.wav"));
    WavWriter w;
    QVERIFY(w.open(path, {16000, 1, 16}));
    w.write(QByteArray(1000, '\0'));
    w.discard();
    QVERIFY(!QFile::exists(path));
}

void WavWriterTest::rejectsInvalidFormat()
{
    QTemporaryDir dir;
    WavWriter w;
    QVERIFY(!w.open(dir.filePath(QStringLiteral("e.wav")), {0, 1, 16}));
    QVERIFY(!w.open(dir.filePath(QStringLiteral("e.wav")), {16000, 0, 16}));
    // 8 bits em WAV é unsigned; o resto do aplicativo é signed.
    QVERIFY(!w.open(dir.filePath(QStringLiteral("e.wav")), {16000, 1, 8}));
    QVERIFY(!w.errorString().isEmpty());
}

void WavWriterTest::finalizeIsIdempotent()
{
    QTemporaryDir dir;
    WavWriter w;
    QVERIFY(w.open(dir.filePath(QStringLiteral("f.wav")), {16000, 1, 16}));
    w.write(QByteArray(100, '\0'));
    QVERIFY(w.finalize());
    // O caminho de erro também chama finalize(); chamar duas vezes não pode
    // corromper o arquivo nem falhar.
    QVERIFY(w.finalize());
}

QTEST_MAIN(WavWriterTest)
#include "WavWriterTest.moc"
