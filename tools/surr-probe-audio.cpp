/**
 * Sonda de áudio — grava 3 s do microfone e imprime o que saiu.
 * Valida a negociação de formato, a conversão e o header do WAV com o
 * hardware real, sem depender de atalho, portal nem rede.
 */
#include "AudioRecorder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>

using namespace surr;

static void say(const QString &s) { std::printf("%s\n", qPrintable(s)); std::fflush(stdout); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    say(QStringLiteral("Microfones disponíveis:"));
    for (const auto &[id, name] : AudioRecorder::availableInputs()) {
        say(QStringLiteral("  • %1").arg(name));
    }
    say(QString());

    AudioRecorder recorder;
    if (argc > 1) {
        recorder.setDeviceId(QString::fromUtf8(argv[1]));
    }

    float maxLevel = 0.0f;
    int levelTicks = 0;
    QObject::connect(&recorder, &AudioRecorder::levelChanged, [&](float level) {
        maxLevel = qMax(maxLevel, level);
        if (level > 0.001f) ++levelTicks;
    });

    QObject::connect(&recorder, &AudioRecorder::captureStarted, [](quint64) {
        say(QStringLiteral("Captura viva — FALE AGORA (3 s)…"));
    });
    QObject::connect(&recorder, &AudioRecorder::captureFailed, [](quint64, const QString &m) {
        say(QStringLiteral("FALHOU: %1").arg(m));
        qApp->exit(1);
    });
    QObject::connect(&recorder, &AudioRecorder::captureFinished,
                     [&](quint64, const QString &path, double seconds) {
        const QFileInfo info(path);
        say(QString());
        say(QStringLiteral("WAV: %1").arg(path));
        say(QStringLiteral("  duração ...... %1 s").arg(seconds, 0, 'f', 2));
        say(QStringLiteral("  tamanho ...... %1 bytes").arg(info.size()));
        say(QStringLiteral("  permissões ... %1")
                .arg(QString::number(static_cast<uint>(info.permissions()) & 0777, 8)));
        say(QStringLiteral("  nível máximo . %1 (%2 amostras acima de zero)")
                .arg(maxLevel, 0, 'f', 3).arg(levelTicks));
        say(QStringLiteral("  esperado ..... %1 bytes (44 + 16000*2*duração)")
                .arg(44 + int(seconds * 32000)));
        qApp->quit();
    });

    QTimer::singleShot(200, [&] { recorder.start(1); });
    QTimer::singleShot(3400, [&] { recorder.stop(1); });
    QTimer::singleShot(15'000, [] { say(QStringLiteral("tempo esgotado")); qApp->exit(2); });

    return app.exec();
}
