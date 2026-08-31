#pragma once

#include <QObject>
#include <QString>

namespace surr {

class ClipboardManager;
class EisInjector;

/**
 * Entrega o texto: área de transferência primeiro, colagem depois.
 *
 * A ordem é obrigatória e é o que faz o requisito principal funcionar:
 *
 *  1. publica o texto na área de transferência (pelo portal);
 *  2. **espera a confirmação do portal** — não um timer, uma resposta, o que
 *     estabelece de fato um "aconteceu antes";
 *  3. espera uma folga curta para o compositor assentar;
 *  4. emite Ctrl+V no aplicativo que estava em foco.
 *
 * Se a colagem falhar, o texto continua na área de transferência. Nunca se
 * perde uma transcrição por causa de um aplicativo que ignora colagem
 * sintética — o usuário sempre pode colar à mão.
 */
class DeliveryCoordinator : public QObject
{
    Q_OBJECT
public:
    DeliveryCoordinator(ClipboardManager *clipboard, EisInjector *injector,
                        QObject *parent = nullptr);

    void setAutoPaste(bool enabled);
    void setTerminalMode(bool enabled);
    void setPasteDelayMs(int ms);

    /// Entrega @p text. O resultado chega por `finished()` ou `failed()`.
    void deliver(quint64 runId, const QString &text);

Q_SIGNALS:
    /// @p pasted false = só copiado (colagem indisponível ou desligada).
    void finished(quint64 runId, bool pasted, const QString &note);
    void failed(quint64 runId, const QString &message);

private:
    struct Private;
    Private *d;
};

} // namespace surr
