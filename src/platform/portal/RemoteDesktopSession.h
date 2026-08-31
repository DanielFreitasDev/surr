#pragma once

#include <QObject>
#include <QString>

namespace surr {

/**
 * Sessão RemoteDesktop do portal, somente teclado, com área de transferência.
 *
 * A ordem das chamadas é obrigatória e não é óbvia:
 *
 *   CreateSession
 *   → SelectDevices(types=KEYBOARD, persist_mode=2, restore_token?)
 *   → RequestClipboard          ← precisa vir ANTES do Start
 *   → Start                     ← é aqui que aparece o diálogo
 *   → guardar o NOVO restore_token (é de uso único e roda a cada sessão)
 *   → ConnectToEIS              ← antes de qualquer tráfego de clipboard
 *
 * Dois detalhes que custam caro se ignorados:
 *
 * - O backend do KDE recusa `ConnectToEIS` numa sessão já "enabled" pelo
 *   caminho legado `NotifyKeyboard*`. Este código nunca usa `NotifyKeyboard*`,
 *   e chama o EIS antes do clipboard como seguro adicional.
 * - O flag de clipboard entra no `restore_token`. Mudar se pedimos ou não
 *   clipboard invalida todos os tokens guardados e força novo diálogo.
 *   Decisão travada: **sempre pedir clipboard**.
 */
class RemoteDesktopSession : public QObject
{
    Q_OBJECT
public:
    explicit RemoteDesktopSession(QObject *parent = nullptr);
    ~RemoteDesktopSession() override;

    /// Token de restauração guardado de uma sessão anterior (pode ser vazio).
    void setRestoreToken(const QString &token);
    QString restoreToken() const;

    /**
     * Abre a sessão.
     * @param parentWindow  "" para diálogo solto, ou "wayland:<handle>".
     * @param interactive   false só tenta restaurar; se não houver token
     *                      válido, falha em vez de abrir diálogo. É o modo
     *                      usado na partida, para nunca roubar foco sozinho.
     */
    void open(const QString &parentWindow = QString(), bool interactive = true);

    void close();

    bool isActive() const;
    bool hasKeyboard() const;
    bool hasClipboard() const;
    QString sessionHandle() const;

Q_SIGNALS:
    /// Sessão pronta. @p eisFd já é um dup() de posse do chamador.
    void ready(int eisFd);
    /// Falhou ou foi recusada. @p userCancelled distingue recusa de erro.
    void failed(const QString &message, bool userCancelled);
    /// Um novo token foi emitido e precisa ser persistido.
    void restoreTokenChanged(const QString &token);
    /// A sessão morreu (portal reiniciou, usuário revogou).
    void closed();

private Q_SLOTS:
    /// A sessão morreu do outro lado: portal reiniciado ou permissão revogada.
    void onSessionClosed(const QVariantMap &details);

private:
    void selectDevices();
    void requestClipboard();
    void start();
    void connectToEis();
    void fail(const QString &message, bool userCancelled = false);

    struct Private;
    Private *d;
};

} // namespace surr
