#include "DeliveryCoordinator.h"

#include "ClipboardManager.h"
#include "EisInjector.h"
#include "SurrDebug.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTimer>

namespace surr {

struct DeliveryCoordinator::Private {
    ClipboardManager *clipboard = nullptr;
    EisInjector *injector = nullptr;
    bool autoPaste = true;
    bool terminalMode = false;
    int pasteDelayMs = 90;
    quint64 runId = 0;
    bool waiting = false;
};

DeliveryCoordinator::DeliveryCoordinator(ClipboardManager *clipboard, EisInjector *injector,
                                         QObject *parent)
    : QObject(parent), d(new Private)
{
    d->clipboard = clipboard;
    d->injector = injector;

    connect(clipboard, &ClipboardManager::succeeded, this, [this] {
        if (!d->waiting) {
            return;
        }
        const quint64 id = d->runId;

        if (!d->autoPaste) {
            d->waiting = false;
            Q_EMIT finished(id, false, QStringLiteral("Copiado"));
            return;
        }
        if (!d->injector || !d->injector->isReady()) {
            d->waiting = false;
            Q_EMIT finished(id, false,
                            QStringLiteral("Copiado — colagem automática indisponível"));
            return;
        }

        // Folga curta para o compositor assentar antes das teclas.
        QTimer::singleShot(d->pasteDelayMs, this, [this] {
            if (!d->waiting) {
                return;
            }
            if (d->terminalMode) {
                d->injector->sendTerminalPaste();
            } else {
                d->injector->sendPaste();
            }
        });
    });

    connect(clipboard, &ClipboardManager::failed, this, [this](const QString &message) {
        if (!d->waiting) {
            return;
        }
        // Último recurso: o QClipboard só funciona se o processo tiver uma
        // superfície com foco, o que normalmente não é o caso. Ainda assim é
        // melhor tentar do que perder o texto.
        if (auto *cb = QGuiApplication::clipboard()) {
            cb->setText(d->clipboard->currentText());
        }
        d->waiting = false;
        qCWarning(SURR_PORTAL) << "área de transferência pelo portal falhou:" << message;
        Q_EMIT failed(d->runId,
                      QStringLiteral("Não foi possível colocar o texto na área de "
                                     "transferência."));
    });

    connect(injector, &EisInjector::injected, this, [this](bool ok, const QString &message) {
        if (!d->waiting) {
            return;
        }
        d->waiting = false;
        const quint64 id = d->runId;
        if (ok) {
            Q_EMIT finished(id, true, QString());
        } else {
            // O texto está na área de transferência: isto é um aviso, não
            // uma perda.
            qCWarning(SURR_INJECT) << "colagem falhou:" << message;
            Q_EMIT finished(id, false,
                            QStringLiteral("A transcrição foi copiada, mas não foi possível "
                                           "colá-la automaticamente."));
        }
    });
}

void DeliveryCoordinator::setAutoPaste(bool enabled) { d->autoPaste = enabled; }
void DeliveryCoordinator::setTerminalMode(bool enabled) { d->terminalMode = enabled; }
void DeliveryCoordinator::setPasteDelayMs(int ms) { d->pasteDelayMs = qMax(0, ms); }

void DeliveryCoordinator::deliver(quint64 runId, const QString &text)
{
    d->runId = runId;
    d->waiting = true;

    if (!d->clipboard->isAvailable()) {
        if (auto *cb = QGuiApplication::clipboard()) {
            cb->setText(text);
        }
        d->waiting = false;
        Q_EMIT finished(runId, false,
                        QStringLiteral("Copiado — autorize a colagem nas Configurações"));
        return;
    }

    d->clipboard->setText(text);
}

} // namespace surr
