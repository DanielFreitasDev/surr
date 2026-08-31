#include "OpenAiCompatibleProvider.h"

#include "ProviderErrors.h"
#include "SurrDebug.h"
#include "surr_config.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace surr {

namespace {
constexpr int kDefaultTimeoutMs = 90'000;

QHttpPart textPart(const QString &name, const QString &value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QVariant(QStringLiteral("form-data; name=\"%1\"").arg(name)));
    part.setBody(value.toUtf8());
    return part;
}
} // namespace

struct OpenAiCompatibleProvider::Private {
    QNetworkAccessManager *nam = nullptr;
    QString apiKey;
    QUrl endpoint;
    int timeoutMs = kDefaultTimeoutMs;

    QNetworkReply *reply = nullptr;
    QHttpMultiPart *multiPart = nullptr;
    QTimer *timeout = nullptr;
    QElapsedTimer clock;
    QString model;
    qint64 audioBytes = 0;
};

OpenAiCompatibleProvider::OpenAiCompatibleProvider(QNetworkAccessManager *nam, QObject *parent)
    : ITranscriptionProvider(parent), d(new Private)
{
    d->nam = nam;
    d->timeout = new QTimer(this);
    d->timeout->setSingleShot(true);
    connect(d->timeout, &QTimer::timeout, this, [this] {
        if (d->reply) {
            // `abort()` produz OperationCanceledError; marcar antes para que
            // o handler saiba que foi estouro de tempo, não cancelamento.
            qCInfo(SURR_NET) << "requisição abortada por timeout";
            d->reply->setProperty("surrTimedOut", true);
            d->reply->abort();
        }
    });
}

OpenAiCompatibleProvider::~OpenAiCompatibleProvider()
{
    cleanup();
    delete d;
}

void OpenAiCompatibleProvider::setApiKey(const QString &key) { d->apiKey = key.trimmed(); }
bool OpenAiCompatibleProvider::hasApiKey() const { return !d->apiKey.isEmpty(); }

void OpenAiCompatibleProvider::setEndpoint(const QUrl &url) { d->endpoint = url; }
QUrl OpenAiCompatibleProvider::endpoint() const { return d->endpoint; }

void OpenAiCompatibleProvider::setTimeoutMs(int ms) { d->timeoutMs = qMax(1000, ms); }

bool OpenAiCompatibleProvider::isConfigured() const
{
    return hasApiKey() && d->endpoint.isValid();
}

bool OpenAiCompatibleProvider::isBusy() const { return d->reply != nullptr; }

QByteArray OpenAiCompatibleProvider::userAgent() const
{
    return QByteArrayLiteral("surr/") + QByteArrayLiteral(SURR_VERSION_STRING);
}

void OpenAiCompatibleProvider::addExtraFields(QHttpMultiPart *, const Options &) {}

void OpenAiCompatibleProvider::transcribe(const QString &wavPath, const Options &options)
{
    if (isBusy()) {
        TranscriptionFailure f;
        f.kind = TranscriptionError::BadRequest;
        f.message = QStringLiteral("Já há uma transcrição em andamento.");
        QMetaObject::invokeMethod(this, [this, f] { Q_EMIT failed(f); }, Qt::QueuedConnection);
        return;
    }

    if (!isConfigured()) {
        TranscriptionFailure f;
        f.kind = TranscriptionError::NotConfigured;
        f.message = QStringLiteral("A chave de %1 não está configurada.").arg(displayName());
        QMetaObject::invokeMethod(this, [this, f] { Q_EMIT failed(f); }, Qt::QueuedConnection);
        return;
    }

    auto *file = new QFile(wavPath);
    if (!file->open(QIODevice::ReadOnly)) {
        const QString detail = file->errorString();
        delete file;
        TranscriptionFailure f;
        f.kind = TranscriptionError::BadRequest;
        f.message = QStringLiteral("Não foi possível ler o áudio gravado.");
        f.detail = detail;
        QMetaObject::invokeMethod(this, [this, f] { Q_EMIT failed(f); }, Qt::QueuedConnection);
        return;
    }

    const qint64 size = file->size();
    if (size > maxUploadBytes()) {
        file->close();
        delete file;
        TranscriptionFailure f;
        f.kind = TranscriptionError::PayloadTooLarge;
        f.message = QStringLiteral("A gravação (%1 MB) passa do limite de %2 MB do serviço.")
                        .arg(size / (1024.0 * 1024.0), 0, 'f', 1)
                        .arg(maxUploadBytes() / (1024 * 1024));
        QMetaObject::invokeMethod(this, [this, f] { Q_EMIT failed(f); }, Qt::QueuedConnection);
        return;
    }

    d->audioBytes = size;
    d->model = options.model.isEmpty() ? defaultModel() : options.model;

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    file->setParent(multiPart); // o multipart passa a ser dono do arquivo

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("audio/wav")));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                                    .arg(QFileInfo(wavPath).fileName())));
    filePart.setBodyDevice(file);
    multiPart->append(filePart);

    multiPart->append(textPart(QStringLiteral("model"), d->model));
    multiPart->append(textPart(QStringLiteral("temperature"),
                               QString::number(options.temperature, 'f', 1)));
    multiPart->append(textPart(QStringLiteral("response_format"),
                               options.verbose ? QStringLiteral("verbose_json")
                                               : QStringLiteral("json")));

    // Idioma vazio = detecção automática: o parâmetro é omitido, não enviado
    // em branco. A API espera ISO-639-1 ("pt"), nunca "pt-BR".
    if (!options.language.isEmpty()) {
        multiPart->append(textPart(QStringLiteral("language"), options.language));
    }
    if (!options.prompt.isEmpty()) {
        multiPart->append(textPart(QStringLiteral("prompt"), options.prompt.left(1000)));
    }
    addExtraFields(multiPart, options);

    QNetworkRequest request(d->endpoint);
    request.setRawHeader("Authorization", "Bearer " + d->apiKey.toUtf8());
    request.setRawHeader("User-Agent", userAgent());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    d->multiPart = multiPart;
    d->clock.start();
    d->reply = d->nam->post(request, multiPart);
    multiPart->setParent(d->reply);

    connect(d->reply, &QNetworkReply::finished, this, &OpenAiCompatibleProvider::handleReply);
    d->timeout->start(d->timeoutMs);

    qCDebug(SURR_NET) << "transcrição enviada" << id() << d->model << size << "bytes";
}

void OpenAiCompatibleProvider::cancel()
{
    if (d->reply) {
        d->reply->abort();
    }
}

void OpenAiCompatibleProvider::cleanup()
{
    d->timeout->stop();
    if (d->reply) {
        d->reply->deleteLater(); // o multipart é filho do reply
        d->reply = nullptr;
    }
    d->multiPart = nullptr;
}

void OpenAiCompatibleProvider::finishWithFailure(const TranscriptionFailure &failure)
{
    cleanup();
    Q_EMIT failed(failure);
}

void OpenAiCompatibleProvider::handleReply()
{
    QNetworkReply *reply = d->reply;
    if (!reply) {
        return;
    }
    d->timeout->stop();

    const auto latency = std::chrono::milliseconds(d->clock.elapsed());
    const QByteArray body = reply->readAll();
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netError = reply->error();
    const bool timedOut = reply->property("surrTimedOut").toBool();

    if (netError != QNetworkReply::NoError && status == 0) {
        const auto effective =
            timedOut ? QNetworkReply::TimeoutError : netError;
        finishWithFailure(
            ProviderErrors::fromNetworkError(effective, reply->errorString(), displayName()));
        return;
    }

    if (status < 200 || status >= 300) {
        int retryAfter = 0;
        const QByteArray header = reply->rawHeader("Retry-After");
        if (!header.isEmpty()) {
            retryAfter = header.toInt();
        }
        finishWithFailure(
            ProviderErrors::fromHttpStatus(status, body, displayName(), retryAfter));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        TranscriptionFailure f;
        f.kind = TranscriptionError::MalformedResponse;
        f.message = QStringLiteral("O serviço devolveu uma resposta que não pôde ser lida.");
        f.detail = parseError.errorString();
        f.httpStatus = status;
        finishWithFailure(f);
        return;
    }

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("text"))) {
        TranscriptionFailure f;
        f.kind = TranscriptionError::MalformedResponse;
        f.message = QStringLiteral("O serviço respondeu sem o texto transcrito.");
        f.httpStatus = status;
        finishWithFailure(f);
        return;
    }

    const QString text = root.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        TranscriptionFailure f;
        f.kind = TranscriptionError::EmptyResult;
        f.message = QStringLiteral("Nada foi reconhecido no áudio.");
        f.httpStatus = status;
        finishWithFailure(f);
        return;
    }

    TranscriptionResult result;
    result.text = text;
    result.provider = id();
    result.model = d->model;
    result.latency = latency;
    result.audioBytes = d->audioBytes;

    cleanup();
    // Nunca registrar o texto transcrito: é fala do usuário.
    qCDebug(SURR_NET) << "transcrição concluída" << id() << latency.count() << "ms"
                      << text.size() << "caracteres";
    Q_EMIT finished(result);
}

} // namespace surr
