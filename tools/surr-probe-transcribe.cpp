/**
 * Sonda de transcrição — envia um WAV ao provider e imprime o resultado.
 * Valida proxy, KWallet, multipart e a leitura da resposta com o serviço real.
 *
 * Uso: ./surr-probe-transcribe <arquivo.wav> [groq|cepify]
 */
#include "CepifyProvider.h"
#include "GroqProvider.h"
#include "ProxyResolver.h"
#include "SecretStore.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QTimer>

#include <cstdio>

using namespace surr;

static void say(const QString &s) { std::printf("%s\n", qPrintable(s)); std::fflush(stdout); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("surr"));

    if (argc < 2) {
        say(QStringLiteral("uso: surr-probe-transcribe <arquivo.wav> [groq|cepify]"));
        return 2;
    }
    const QString wav = QString::fromUtf8(argv[1]);
    const QString which = argc > 2 ? QString::fromUtf8(argv[2]) : QStringLiteral("groq");

    ProxyResolver::applyToApplication();

    auto *nam = new QNetworkAccessManager(&app);
    auto *secrets = new SecretStore(&app);

    QObject::connect(secrets, &SecretStore::readyChanged, [&](bool) {
        say(QStringLiteral("Carteira: %1")
                .arg(secrets->isUsingFallback() ? QStringLiteral("indisponível (refúgio local)")
                                                : QStringLiteral("aberta")));

        // Importa a chave que o aplicativo anterior deixou em texto puro.
        if (!secrets->has(QLatin1String(SecretStore::kGroqKey))
            && !SecretStore::groqKeyComesFromEnvironment()) {
            const QString legacy = SecretStore::findLegacyGroqKey();
            if (!legacy.isEmpty() && secrets->write(QLatin1String(SecretStore::kGroqKey), legacy)) {
                say(QStringLiteral("Chave da Groq importada do sussurro para o KWallet."));
            }
        }

        ITranscriptionProvider *provider = nullptr;
        if (which == QLatin1String("cepify")) {
            auto *p = new CepifyProvider(nam, &app);
            // O endereço não tem padrão no código; vem do mesmo surrrc do app.
            p->setBaseUrl(Settings().cepifyBaseUrl());
            p->setApiKey(secrets->read(QLatin1String(SecretStore::kCepifyKey)));
            provider = p;
        } else {
            auto *p = new GroqProvider(nam, &app);
            p->setApiKey(secrets->groqApiKey());
            provider = p;
        }

        say(QStringLiteral("Provider: %1 — configurado: %2")
                .arg(provider->displayName(),
                     provider->isConfigured() ? QStringLiteral("sim") : QStringLiteral("NÃO")));

        QObject::connect(provider, &ITranscriptionProvider::finished,
                         [](const TranscriptionResult &r) {
            say(QString());
            say(QStringLiteral("=== TRANSCRIÇÃO OK ==="));
            say(QStringLiteral("  provider ... %1").arg(r.provider));
            say(QStringLiteral("  modelo ..... %1").arg(r.model));
            say(QStringLiteral("  latência ... %1 ms").arg(r.latency.count()));
            say(QStringLiteral("  áudio ...... %1 bytes").arg(r.audioBytes));
            say(QStringLiteral("  texto ...... \"%1\"").arg(r.text));
            qApp->quit();
        });
        QObject::connect(provider, &ITranscriptionProvider::failed,
                         [](const TranscriptionFailure &f) {
            say(QString());
            say(QStringLiteral("=== FALHOU ==="));
            say(QStringLiteral("  mensagem ... %1").arg(f.message));
            say(QStringLiteral("  http ....... %1").arg(f.httpStatus));
            say(QStringLiteral("  detalhe .... %1").arg(f.detail));
            qApp->exit(1);
        });

        ITranscriptionProvider::Options opts;
        opts.language = QStringLiteral("pt");
        say(QStringLiteral("Enviando %1…").arg(wav));
        provider->transcribe(wav, opts);
    });

    secrets->open();
    QTimer::singleShot(120'000, [] { say(QStringLiteral("tempo esgotado")); qApp->exit(3); });
    return app.exec();
}
