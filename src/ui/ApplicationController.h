#pragma once

#include <QObject>

namespace surr {

/**
 * Liga todas as peças. Não implementa política própria: a máquina de estados
 * decide, e este objeto executa e reporta.
 *
 * Regra que atravessa o arquivo: **nada de event loop aninhado na thread
 * principal**. Toda chamada ao portal é assíncrona, e é isso que garante que
 * `Activated` e `Deactivated` cheguem na ordem em que aconteceram.
 */
class ApplicationController : public QObject
{
    Q_OBJECT
public:
    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController() override;

    /// Sobe tudo. Retorna false se algo essencial faltar.
    bool start();

public Q_SLOTS:
    void openSettings();
    void shutdown();

    // Expostos no barramento para o processo `--settings`, que não pode
    // manter a sessão do portal: ela morreria junto com aquela janela.
    Q_SCRIPTABLE void authorizePaste();
    Q_SCRIPTABLE void configureShortcut();
    Q_SCRIPTABLE QString triggerDescription() const;

private:
    /// Remove o WAV temporário do ciclo, salvo em modo de depuração.
    void cleanupAudio();

    struct Private;
    Private *d;
};

} // namespace surr
