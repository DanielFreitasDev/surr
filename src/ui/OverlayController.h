#pragma once

#include <QObject>
#include <QString>

class QScreen;

class QQuickView;

namespace surr {

/**
 * A cápsula de gravação.
 *
 * Regra que manda em tudo aqui: **a cápsula nunca pode receber foco**. Se
 * receber, o Ctrl+V da entrega chega nela em vez de chegar no aplicativo em
 * que o usuário estava digitando. Daí `KeyboardInteractivityNone`, zona
 * exclusiva zero e transparência a mouse.
 *
 * Usa layer-shell porque no Wayland uma janela comum não escolhe onde fica —
 * quem decide é o compositor. O aplicativo anterior, sem binding de
 * layer-shell na sua linguagem, teve de rodar a interface inteira sobre
 * XWayland só para posicionar esta cápsula.
 *
 * `LayerShellQt::Shell::useLayerShell()` vale para o processo inteiro, então
 * este processo só tem esta janela; as Configurações rodam em outro processo.
 */
class OverlayController : public QObject
{
    Q_OBJECT
public:
    enum class State { Hidden, Preparing, Recording, Transcribing, Success, Error };
    Q_ENUM(State)

    explicit OverlayController(QObject *parent = nullptr);
    ~OverlayController() override;

    /// Cria a janela. Chamar depois de `Shell::useLayerShell()`.
    bool initialize();

public Q_SLOTS:
    /// Aparece em "preparando": o microfone ainda não entregou amostras.
    void beginRecording();
    /// As amostras começaram a chegar — só agora dizemos "gravando".
    void markLive();
    void setLevel(float level);
    void showTranscribing();
    void showSuccess(const QString &message);
    void showError(const QString &message);
    void dismiss();

private:
    void applyLayerShell();
    /// Pergunta ao KWin qual é a tela ativa e então mostra a cápsula.
    void showOnActiveScreen();
    void placeOn(QScreen *screen);
    void setState(State state);

    struct Private;
    Private *d;
};

} // namespace surr
