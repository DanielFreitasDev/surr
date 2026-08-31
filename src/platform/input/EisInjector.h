#pragma once

#include <QObject>

namespace surr {

/**
 * Envia teclas por EIS/libei, o caminho recomendado hoje pelo portal.
 *
 * Só emite a combinação de colagem. O texto inteiro já está na área de
 * transferência, então digitar caractere a caractere seria trocar um
 * problema resolvido por vários: Unicode, layout ABNT2, dead keys, acentos,
 * velocidade.
 *
 * A fila da libei é integrada ao event loop do Qt por `QSocketNotifier` —
 * nunca por timer de polling, para o aplicativo não gastar CPU parado.
 */
class EisInjector : public QObject
{
    Q_OBJECT
public:
    explicit EisInjector(QObject *parent = nullptr);
    ~EisInjector() override;

    /// Assume a posse de @p eisFd (vindo de `RemoteDesktopSession::ready`).
    bool attach(int eisFd);
    void detach();

    /// True quando há um teclado virtual pronto para receber eventos.
    bool isReady() const;

    /// Emite Ctrl+V. O resultado chega por `injected()`.
    void sendPaste();

    /// Emite Ctrl+Shift+V (colagem em terminais).
    void sendTerminalPaste();

Q_SIGNALS:
    void readyChanged(bool ready);
    void injected(bool ok, const QString &message);
    /// A conexão EIS caiu; quem ouve deve reconstruir a sessão do portal.
    void disconnected();

private Q_SLOTS:
    void onReadable();
    void onStep();

private:
    void drainEvents();
    void beginChord(bool withShift);
    void resolveKeycodes();

    struct Private;
    Private *d;
};

} // namespace surr
