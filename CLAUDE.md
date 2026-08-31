@LEARNING.md

# surr

Ditado por voz push-to-talk nativo para **KDE Plasma 6 no Wayland**. Segure a
tecla (Pause), fale, solte: o áudio vai para uma API Whisper (Groq ou servidor
próprio), o texto volta, é copiado e colado onde o cursor já estava.

O problema que ele resolve é específico do Wayland: **o compositor é dono do
teclado e da área de transferência**, então o ferramental de X11 (`xdotool`,
XTest, `/dev/uinput`) não funciona — e o pior é que falha *em silêncio*, sem
exceção e sem código de erro. Por isso toda entrada e saída passa por portais
XDG (`GlobalShortcuts`, `RemoteDesktop`, `Clipboard`) e por libei.

Leituras de apoio: `README.md` (uso, instalação, providers) e
`docs/ACHADOS-DO-AMBIENTE.md` — as medições brutas do ambiente real, com
comando e saída, que justificam as decisões. Quando algo aqui parecer
arbitrário, a resposta costuma estar lá.

---

## Comandos

```bash
# Configurar (o cache atual aponta para ~/.local; o binário instalado é
# ~/.local/bin/surr e é ele que está no PATH)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$HOME/.local"

cmake --build build
cmake --install build            # NÃO é opcional — ver "Instalar é requisito"

ctest --test-dir build --output-on-failure
ctest --test-dir build -R statemachine --output-on-failure   # um alvo só
./build/bin/statemachine                                      # binário direto
```

Alvos de teste: `wavwriter`, `statemachine`, `provider`, `portalpaths`,
`pcmconverter`, `proxyresolver`, `settings` (mais `appstreamtest`, do ECM).

### Depurar

```bash
QT_LOGGING_RULES="surr.*.debug=true" surr
```

Categorias: `surr.app`, `surr.audio`, `surr.net`, `surr.portal`,
`surr.shortcut`, `surr.inject`, `surr.ui`. Todas nascem em `QtInfoMsg`.

```bash
surr --preview                     # percorre os estados da cápsula, sem API
./build/bin/surr-probe             # o Deactivated do portal chega ao soltar?
./build/bin/surr-probe-audio       # grava 3 s e valida o WAV
./build/bin/surr-probe-paste       # cadeia completa: sessão → clipboard → Ctrl+V
./build/bin/surr-probe-transcribe <arquivo.wav> [groq|cepify]

# Testemunha independente do atalho (no Wayland não dá para simular a tecla)
dbus-monitor --session "type='signal',interface='org.freedesktop.portal.GlobalShortcuts'"

# Cenário do autostart, que não herda o proxy do .zshrc
env -u http_proxy -u https_proxy -u all_proxy surr
```

### Reiniciar o daemon

`pgrep -f`/`pkill -f` casam o **próprio shell** e o matam (saída 144).
Aconteceu duas vezes no histórico deste projeto. A forma segura:

```bash
for pid in $(pgrep -x surr); do
  [ "$pid" = "$$" ] && continue
  [ "$pid" = "$PPID" ] && continue
  kill -TERM "$pid"
done
```

`pgrep -xc surr` = 2 é normal: é o daemon **mais** a janela `surr --settings`,
que roda em processo separado de propósito.

---

## Convenções

- **Idioma:** pt-BR em comentários, documentação, strings de interface e
  mensagens de commit. Identificadores em inglês.
- **O comentário explica o *porquê*, quase sempre citando a medição que o
  motivou.** Nada de comentário que repete o que a linha já diz. Se você
  descobrir o motivo de algo, o comentário vai junto do código *e* a entrada
  vai para o `LEARNING.md`.
- Namespace `surr::`; PIMPL (`struct Private`) nas classes de peso; sinais e
  slots com `Q_SIGNALS`/`Q_SLOTS` (o build usa `QT_NO_KEYWORDS`).
- O build compila com `-Werror=return-type`, `QT_NO_CAST_FROM_ASCII` e
  `QT_STRICT_ITERATORS`: use `QStringLiteral`/`QLatin1String`, nunca literal
  cru.
- `Settings` lê e grava por meio das macros `SURR_GET`/`SURR_SET`; toda chave
  nova precisa de um padrão utilizável sem o usuário abrir as configurações.

## Arquitetura: o que não é óbvio

**A separação em três bibliotecas é imposta pelo linker, não é decoração.**
`surr_core` linka só `Qt6::Core` e `Qt6::Network`. Um `#include <QDBusMessage>`
perdido na máquina de estados vira **erro de compilação** — é isso que mantém
a suíte de testes rodando sem sessão gráfica. Não adicione DBus, Gui ou
Multimedia a `surr_core` para "resolver" um include.

**Nenhum event loop aninhado na thread principal.** Toda chamada de portal e o
`KWallet::openWallet` são assíncronos. Uma chamada bloqueante roda um event
loop aninhado e pode **reordenar os sinais `Activated`/`Deactivated`** do
portal — ou seja, quebrar o push-to-talk. A única exceção deliberada é o
`Registry.Register` no `main.cpp`, que precisa terminar antes de qualquer
outra conversa com o portal.

**Instalar é requisito, não conveniência.** O `Registry.Register` resolve o
app-id com `g_desktop_app_info_new()`, que devolve `NULL` se não existir um
`.desktop` instalado **ou** se o binário do `Exec` não estiver no `PATH` — e o
`PATH` do portal (serviço systemd de usuário) não inclui `~/.local/bin`. Daí o
`.desktop` ser gerado pelo CMake com caminho absoluto. Rodando de `build/` sem
instalar, a permissão de colagem é guardada sob o app-id de quem lançou o
processo (foi assim que a permissão do app anterior foi parar em
`org.kde.konsole`).

**O `restore_token` do portal é de uso único.** O `Start` devolve um novo a
cada restauração bem-sucedida; não gravar o novo perde a permissão na próxima
vez. Ele mora no KWallet junto das chaves de API, porque é uma capacidade de
injetar teclas — não um dado comum.

**Dois processos de propósito.** `LayerShellQt::Shell::useLayerShell()` vale
para o processo inteiro: toda janela vira superfície de layer-shell. Por isso
o daemon tem só a cápsula e `--settings` roda separado, conversando por D-Bus
em `/Control` (`br.com.cepify.Surr.Control`). Caminho próprio porque
`/MainApplication` já pertence ao `KDBusService`.

Só que a separação **não basta sozinha**: `useLayerShell()` é um
`qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell")`, e o
`QProcess::startDetached` repassa o ambiente ao filho. Por isso o `main.cpp`
faz `qunsetenv` dessa variável no modo `--settings`. Antes de confiar que um
processo separado isola alguma coisa, verifique se a biblioteca não trabalha
por variável de ambiente.

**Configuração não atravessa os dois processos sozinha.** A janela de
configurações trabalha num rascunho em memória; só o Aplicar/OK grava
(`KConfig::sync()`, via `Settings::save()`) e chama `/Control.reloadSettings`,
que faz `reparseConfiguration()` no daemon e repassa os valores em
`applyLiveSettings()`. Sem esse aviso o daemon seguiria com o que leu na
partida — o KConfig não percebe que outro processo gravou. Pelo mesmo motivo o
`shutdown()` do daemon **não** chama `save()`: despejaria a cópia velha por
cima do que a janela acabou de gravar. **Valor novo que alguma peça guarde em
cópia entra no `applyLiveSettings()`** — senão ele só passa a valer no próximo
boot, e isso não dá erro nenhum.

**A máquina de estados não conhece áudio, rede nem UI.** Mudança de política
(o que conta como ditado válido, o que acontece num erro) vai nela; o
`ApplicationController` só liga peças e reporta. É o que torna as corridas de
apertar/soltar testáveis — no Wayland não dá para simular a tecla.

**Contrato QML↔C++ por inteiro cru.** `Overlay.qml` recebe `state` como `int`
espelhando `OverlayController::State`. Reordenar o enum quebra a cápsula em
silêncio; os dois lados mudam juntos.

**Regra de privacidade no log:** nunca registrar o texto transcrito, amostras
de áudio, chaves de API ou restore tokens. Só tamanhos, durações e códigos.
O WAV é apagado logo após o envio, salvo com **Modo de depuração** ligado.

**Nenhuma falha vira texto colado.** Mensagem de erro só aparece na cápsula, e
se a colagem falhar o texto **continua na área de transferência** — nunca se
perde uma transcrição.

### Onde o estado mora

| O quê | Onde |
|---|---|
| Configuração | `~/.config/surrrc` (KConfig) — gravado só no Aplicar/OK |
| Chaves de API e `restore_token` | KWallet, pasta `surr` (refúgio `secrets.json` 0600) |
| Autostart | `~/.config/autostart/br.com.cepify.Surr.desktop` (cópia do instalado) |
| WAV do ciclo | `$XDG_RUNTIME_DIR/surr/ditado-<runId>.wav` |

`GROQ_API_KEY` no ambiente vence o KWallet — conveniência de desenvolvimento,
nunca gravada em disco.

---

## LEARNING.md — regras

O `LEARNING.md` é a base de conhecimento cumulativa deste projeto, e está
importado na **primeira linha** deste arquivo (`@LEARNING.md`), portanto entra
no contexto de toda sessão automaticamente. Não depende de ninguém "lembrar de
conferir".

**Antes de encerrar qualquer tarefa neste repositório, registre no
`LEARNING.md` o que você aprendeu.** Isso vale para qualquer tipo de
aprendizado, não só bug corrigido:

- resolveu um bug — o que era, e por que a causa não era a óbvia;
- descobriu uma pegadinha de biblioteca, ferramenta, portal ou ambiente;
- tomou uma decisão de arquitetura — registre o motivo, não só a escolha;
- **confirmou que uma abordagem NÃO funciona** — isso vale tanto quanto o que
  funciona, e é o que evita a próxima sessão tentar de novo;
- mediu algo (latência, formato de dispositivo, comportamento do compositor).

Formato fixo por entrada: **Data — Categoria — Problema/Contexto — Solução ou
insight — Por que importa**. Coloque a entrada na seção da categoria dela e
acrescente a linha correspondente no índice do topo.

Antes de investigar qualquer coisa do zero, **escaneie o índice do
`LEARNING.md`** — a resposta pode já estar lá.

Como o arquivo entra inteiro no contexto a cada sessão, ele não pode inchar à
toa: de tempos em tempos, revise, funda entradas repetidas e apague as que
ficaram desatualizadas. Medição bruta e reproduzível continua em
`docs/ACHADOS-DO-AMBIENTE.md`; o `LEARNING.md` fica com a lição.
