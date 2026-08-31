#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QString>

namespace surr {

/**
 * Área de transferência pelo portal, atada à sessão RemoteDesktop.
 *
 * Por que não usar só `QClipboard`: num cliente Wayland nativo,
 * `wl_data_device.set_selection` exige um serial de input vindo de uma
 * superfície com foco. A cápsula do surr é layer-shell com
 * `KeyboardInteractivityNone` e nunca recebe evento de input, então seu
 * serial é 0 e o compositor descarta a chamada **em silêncio**:
 * `QClipboard::setText()` pareceria funcionar e o Ctrl+V seguinte colaria o
 * conteúdo ANTERIOR. O portal não tem essa exigência.
 *
 * O modelo é de posse tardia: declaramos os tipos MIME e o portal pede o
 * conteúdo (`SelectionTransfer`) quando alguém cola. É por isso que o surr
 * precisa continuar rodando depois do ditado — o Klipper, porém, tira um
 * retrato na hora, então o texto sobrevive a um encerramento.
 */
class ClipboardManager : public QObject
{
    Q_OBJECT
public:
    explicit ClipboardManager(QObject *parent = nullptr);
    ~ClipboardManager() override;

    /// Sessão RemoteDesktop já iniciada e com clipboard concedido.
    void setSession(const QString &sessionHandle);
    void clearSession();
    bool isAvailable() const;

    /**
     * Publica @p text. `succeeded()` só é emitido quando o portal confirma
     * a chamada — é essa confirmação, e não um timer, que estabelece o
     * "aconteceu antes" em relação ao Ctrl+V.
     */
    void setText(const QString &text);

    /// Conteúdo atualmente servido por nós.
    QString currentText() const;

Q_SIGNALS:
    void succeeded();
    void failed(const QString &message);

private Q_SLOTS:
    void onSelectionTransfer(const QDBusObjectPath &session, const QString &mimeType,
                             uint serial);

private:
    struct Private;
    Private *d;
};

} // namespace surr
