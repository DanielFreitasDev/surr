# ACEITACAO.md — o contrato do surr

Este arquivo define o que significa **pronto** neste projeto. Quem decide não é
opinião de leitura de código: é o código de saída do `./verificar.sh`.

```bash
./verificar.sh     # sai 0 = PASSOU · sai 1 = FALHOU
```

---

## Serve para

Ditar por voz em qualquer aplicativo do KDE Plasma 6 no Wayland: segurar a tecla
Pause, falar, soltar, e o texto aparecer onde o cursor já estava.

---

## Pronto é isto

Doze checks. Cada um é decidido pela máquina sozinha, sem rede e sem sessão
gráfica. `./verificar.sh` roda todos, imprime ✔/✘ por check e sai 0 só se todos
passarem. Nada aqui altera o projeto, a instalação em `~/.local` ou o
`~/.config/surrrc`: a compilação e a instalação de teste vão para um diretório
temporário, apagado no fim.

| # | Check | O que promete | Como se prova | O que eu teria que quebrar para ficar vermelho |
|---|---|---|---|---|
| 1 | `build` | O repositório compila do zero. | `cmake -S . -B <tmp> -G Ninja -DCMAKE_BUILD_TYPE=Release` + `cmake --build` → rc 0 | Qualquer erro de compilação, dependência faltando, ou um `#include` de DBus/GUI dentro de `src/core/`. |
| 2 | `instala` | O `cmake --install` produz os cinco artefatos que o portal e o Plasma precisam achar. | `cmake --install` num prefixo temporário; existem `bin/surr`, o `.desktop`, o `.metainfo.xml`, o ícone e o `surr.notifyrc` | Apagar um `install(...)` do `data/CMakeLists.txt`. |
| 3 | `testes` | A suíte inteira passa. | `QT_QPA_PLATFORM=offscreen ctest` → rc 0 e `100% tests passed` | Quebrar a máquina de estados, o header do WAV, a conversão de PCM, o parsing dos providers, o mapeamento de erro HTTP, a resolução de proxy ou os padrões do `Settings`. |
| 4 | `camadas` | `surr_core` continua sem GUI, DBus, Quick, Multimedia, KConfig ou KWallet — é isso que mantém os testes rodando sem sessão gráfica. | `nm --undefined-only libsurr_core.a` não cita nenhum símbolo desses | Linkar `Qt6::DBus` em `surr_core` e usá-lo na máquina de estados. |
| 5 | `sem-x11` | Nada de `xdotool`, XTest, `/dev/uinput` ou `ydotool` — no Wayland eles falham **em silêncio**, sem erro e sem exceção. | `grep` em `src/` e `tools/` não acha nenhum | Uma sessão futura "consertar a colagem" chamando `ydotool`. |
| 6 | `sem-bloqueio` | Nenhum event loop aninhado na thread principal, que reordenaria os sinais `Activated`/`Deactivated` do portal e quebraria o push-to-talk. | `grep` não acha `QEventLoop` nem `waitForFinished`; há exatamente **uma** chamada D-Bus bloqueante (`QDBus::Block`), no `src/main.cpp` — o `Registry.Register` | Trocar um `asyncCall` do portal por uma chamada síncrona, ou abrir um `QEventLoop` para "esperar a resposta". |
| 7 | `log-limpo` | Nenhum log registra chave de API, restore token, áudio ou o texto ditado. Só tamanhos, durações e códigos. | Analisador varre todo `qCDebug`/`qCInfo`/`qCWarning`/`qCCritical` de `src/`, ignora o texto entre aspas e os `.size()`/`.length()` | Acrescentar `<< apiKey` ou `<< result.text` num log. (Testado: os três vazamentos plantados foram pegos.) |
| 8 | `qml-enum` | A cápsula mostra o estado certo. Esta é a **única** fronteira do projeto sem rede de proteção do compilador: o estado atravessa como inteiro cru. | `OverlayController::State` e a tabela do `Overlay.qml` batem em nome, ordem e índice, e o QML trata todos os estados | Inserir um estado no meio do enum sem mexer no QML. Hoje isso não gera erro nenhum — a cápsula só passa a mostrar a coisa errada. |
| 9 | `desktop-exec` | A permissão de colagem fica guardada sob o app-id certo. O `PATH` do portal não inclui `~/.local/bin`; com `Exec` relativo o `g_desktop_app_info_new()` devolve `NULL`. | O `Exec=` do `.desktop` instalado é caminho absoluto de um arquivo executável que existe | Trocar `Exec=@SURR_BIN@` por `Exec=surr`. |
| 10 | `desktop-valido` | O `.desktop` obedece ao padrão XDG. | `desktop-file-validate` → rc 0 | Uma chave inválida ou uma categoria inexistente no `.desktop.in`. |
| 11 | `metainfo-valido` | O metainfo **do repositório** é válido. | `appstreamcli validate --no-net data/br.com.cepify.Surr.metainfo.xml` → rc 0 | Quebrar o XML. Existe separado do `appstreamtest` do ECM de propósito: aquele valida a cópia *instalada* e, sem `install_manifest.txt`, imprime *"Not installed yet, skipping"* e passa sem olhar nada. Este não tem como ser pulado. |
| 12 | `binario-sobe` | O executável inicializa de verdade e oferece as opções que o README documenta. | `surr --help` sai 0 e lista `--settings` e `--preview` | Um crash na partida (linkagem, `KAboutData`, i18n) ou remover uma opção documentada. |

---

## Só eu julgo

Nada aqui é reavaliado a cada rodada. É caixinha que eu marco **uma vez**.
O Wayland não deixa simular a tecla Pause, então nenhum script vai testar isto.

- [x] **O ditado funciona de ponta a ponta no uso real.** Cursor no Kate →
      segurar Pause → falar → soltar → o texto aparece no cursor → `Ctrl+V` em
      outro lugar cola o mesmo texto. *Confirmado por mim, e registrado com
      traço completo em `docs/ACHADOS-DO-AMBIENTE.md` §9 e §10.*
- [x] **A tela de configurações é uma janela de verdade e grava o que se muda
      nela.** Borda e botões de fechar/minimizar/maximizar, nada encostando na
      barra de rolagem, *Aplicar* acendendo ao mudar e apagando ao desfazer, e
      o daemon adotando o valor novo sem reiniciar. *Confirmado por mim em
      2026-08-31, sobre o commit `51a85af`.*
- [ ] A cápsula não rouba foco (o cursor continua piscando no Kate durante a
      gravação) e aparece no monitor certo em setup multi-monitor.
- [ ] A colagem funciona nos aplicativos que eu uso — inclusive um app
      XWayland — com acentuação, emoji e múltiplas linhas.
- [x] **Reiniciar o app não faz reaparecer o diálogo de permissão**
      (`restore_token` persistido). *Confirmado por mim em 2026-08-31, sobre o
      commit `364ac1b`: boot completo da máquina, nenhuma caixa de diálogo, e a
      carteira regravada 3 s depois do `EIS conectado` — o token novo entrando
      no lugar do usado.*
- [x] **Autostart: depois de logout/login o ditado funciona sem abrir
      terminal** — inclusive o proxy, que **não** é herdado do `.zshrc` nesse
      cenário. *Confirmado por mim em 2026-08-31, sobre o commit `364ac1b`:
      boot completo, Pause → falar → soltar → texto colado, com o log trazendo
      `surr.net: proxy: do kioslaverc`.*
- [ ] **O ditado de ponta a ponta pelo Whisper próprio.** Pause → falar →
      soltar → o texto do meu servidor aparece no cursor. *Em 2026-09-01 a
      máquina provou tudo menos a tecla: fala sintetizada → `CepifyProvider` →
      o servidor Whisper próprio devolveu o texto certo em 1179 ms, com a chave
      lida do KWallet (`docs/ACHADOS-DO-AMBIENTE.md` §14). O que resta sem
      prova é só a composição com o gatilho — o Wayland não deixa um script
      apertar Pause nem falar ao microfone.*
- [ ] As mensagens de erro (chave inválida, internet fora, timeout, microfone
      ausente, permissão recusada) são compreensíveis para mim.
- [ ] O surr não atrapalha a sessão: não segura a tecla Pause quando está
      fechado, não deixa processo zumbi, não pesa.

---

## NÃO faz parte

- Outro ambiente de desktop, outro compositor Wayland, ou X11. Só KDE Plasma 6
  no Wayland.
- Empacotamento para distribuição (`.deb`, Flatpak, AUR, PPA).
- Tradução para outros idiomas além do pt-BR.
- Qualquer provider de transcrição além da Groq e do servidor Whisper próprio.
- Edição, formatação ou pós-processamento do texto transcrito. O surr entrega o
  que a API devolveu.
- Digitar caractere a caractere. O texto sempre vai pela área de transferência —
  é o que resolve Unicode, layout ABNT2 e dead keys de graça.
- Suporte ao `sussurro` (o predecessor em Python). Foi aposentado.

---

## Limitações aceitas

Nada nesta seção é defeito. Nenhuma rodada de verificação deve relatá-las.

1. **O `verificar.sh` não usa rede.** Não manda áudio para a Groq nem lê chave
   de API. Um check que depende da internet, do proxy ou da Groq acusaria
   "FALHOU" sem o projeto ter defeito nenhum — e esta máquina exige proxy. A
   transcrição ao vivo se prova usando o app, não pelo script.
2. **O `verificar.sh` não fala com o servidor Whisper próprio, e isso não muda.**
   O `CepifyProvider` é exercitado offline pelos testes de parsing, com respostas
   gravadas; o teste ao vivo se faz à mão, pelo `surr-probe-transcribe` ou pelo
   *Testar conexão* da janela. *(O servidor esteve fora do ar — Cloudflare 1033,
   túnel caído — enquanto o projeto era feito. Voltou: em 2026-09-01 o
   `surr-probe-transcribe <wav> cepify` transcreveu fala de verdade em 1179 ms,
   com a chave lida do KWallet — `docs/ACHADOS-DO-AMBIENTE.md` §14. A limitação
   que resta é a do item 1, e é de desenho.)*
3. **Nenhum script consegue apertar a tecla Pause.** No Wayland o compositor é o
   dono do teclado; não há como simular a tecla. Todo o caminho
   atalho → gravação → colagem só se prova com um humano falando. Por isso ele
   está em "Só eu julgo" e não em "Pronto é isto".
4. **Nenhum check exige sessão gráfica, portal, microfone ou KWallet.** Tudo que
   depende desses vive nos probes de diagnóstico (`surr-probe`,
   `surr-probe-audio`, `surr-probe-paste`, `surr-probe-transcribe`), que se
   rodam à mão.
5. **Os checks 5, 6 e 7 leem o código-fonte, não o comportamento.** Eles impedem
   que um padrão proibido seja *reintroduzido*; não provam que o código faz a
   coisa certa. Quem prova isso são os checks 1–3 e o uso real.
6. **O teste `settings` grava configuração de verdade**, redirecionada pelo
   `QStandardPaths::setTestModeEnabled(true)` para a caixa de areia do Qt
   (`~/.qttest`). Ele nunca toca no `~/.config/surrrc`.
7. **Nenhum microfone desta máquina oferece 16 kHz mono nativo.** A captura
   acontece no formato nativo (48 kHz ou 32 kHz, estéreo) e o `PcmConverter` faz
   downmix e reamostragem. É por desenho, não por falta de acabamento.

---

CONGELADO em 93dc02a — 2026-08-31. Só mexer com pedido explícito meu ou defeito
real de uso.
