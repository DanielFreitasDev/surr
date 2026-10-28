# LEARNING.md — base de conhecimento do surr

Tudo que já custou tempo para descobrir neste projeto. **Escaneie o índice
antes de investigar do zero.**

Formato de cada entrada: **Data — Categoria — Problema/Contexto — Solução ou
insight — Por que importa.** Entrada nova vai na seção da categoria dela, e a
linha correspondente entra no índice.

Medição bruta e reproduzível (comando + saída) mora em
`docs/ACHADOS-DO-AMBIENTE.md`. Aqui fica a lição.

---

## Índice

### Bugs e soluções
- [2026-08-31 — No boot, o surr nasce antes do portal e monta as sessões em dobro](#2026-08-31--no-boot-o-surr-nasce-antes-do-portal-e-monta-as-sessões-em-dobro)
- [2026-08-31 — A janela de configurações nunca gravava nada no disco](#2026-08-31--a-janela-de-configurações-nunca-gravava-nada-no-disco)
- [2026-08-31 — A janela de configurações herdava o layer-shell pelo ambiente, não pelo código](#2026-08-31--a-janela-de-configurações-herdava-o-layer-shell-pelo-ambiente-não-pelo-código)
- [2026-09-01 — A chave salva na janela não chegava ao daemon, e o "Testar conexão" dizia OK](#2026-09-01--a-chave-salva-na-janela-não-chegava-ao-daemon-e-o-testar-conexão-dizia-ok)
- [2026-10-28 — O microfone do fone Bluetooth sumia da lista: culpa do backend PipeWire do Qt](#2026-10-28--o-microfone-do-fone-bluetooth-sumia-da-lista-culpa-do-backend-pipewire-do-qt)

### Pegadinhas de ambiente, dependências ou bibliotecas
- [2026-08-31 — Reinício do portal apaga o app-id registrado; do terminal ele vira `org.kde.konsole`](#2026-08-31--reinício-do-portal-apaga-o-app-id-registrado-do-terminal-ele-vira-orgkdekonsole)
- [2026-08-31 — Sair do processo descarta a mensagem D-Bus assíncrona](#2026-08-31--sair-do-processo-descarta-a-mensagem-d-bus-assíncrona)
- [2026-08-31 — O filho de um `ScrollView` precisa de `availableWidth`, não de `parent.width`](#2026-08-31--o-filho-de-um-scrollview-precisa-de-availablewidth-não-de-parentwidth)
- [2026-08-31 — O teste `settings` não roda sem display; os outros rodam](#2026-08-31--o-teste-settings-não-roda-sem-display-os-outros-rodam)
- [2026-08-31 — `appstreamtest` valida o metainfo INSTALADO, não o do repositório](#2026-08-31--appstreamtest-valida-o-metainfo-instalado-não-o-do-repositório)
- [2026-08-31 — O separador de dígitos do C++ quebra varredores de código](#2026-08-31--o-separador-de-dígitos-do-c-quebra-varredores-de-código)
- [2026-08-31 — `PIPESTATUS` é do bash; no zsh o veredito do `verificar.sh` some](#2026-08-31--pipestatus-é-do-bash-no-zsh-o-veredito-do-verificarsh-some)
- [2026-10-28 — Mover a pasta do projeto inutiliza o `build/`](#2026-10-28--mover-a-pasta-do-projeto-inutiliza-o-build)
- [2026-10-28 — Capturas para o README: cápsula pelo `--preview`, janela pelo `spectacle -a`](#2026-10-28--capturas-para-o-readme-cápsula-pelo---preview-janela-pelo-spectacle--a)

### Decisões de arquitetura
- [2026-08-31 — Quem derruba a sessão do portal tem de ser um caminho só](#2026-08-31--quem-derruba-a-sessão-do-portal-tem-de-ser-um-caminho-só)
- [2026-08-31 — O estado da cápsula atravessa a fronteira QML↔C++ como inteiro cru](#2026-08-31--o-estado-da-cápsula-atravessa-a-fronteira-qmlc-como-inteiro-cru)
- [2026-08-31 — O veredito de "pronto" é o código de saída do `verificar.sh`](#2026-08-31--o-veredito-de-pronto-é-o-código-de-saída-do-verificarsh)
- [2026-08-31 — O projeto está CONGELADO: auditoria nova só com defeito real de uso](#2026-08-31--o-projeto-está-congelado-auditoria-nova-só-com-defeito-real-de-uso)
- [2026-10-28 — O servidor Whisper próprio não tem endereço padrão no código](#2026-10-28--o-servidor-whisper-próprio-não-tem-endereço-padrão-no-código)

### Abordagens que NÃO funcionaram
_(vazio)_

### Notas de performance
_(vazio)_

---

## Bugs e soluções

### 2026-08-31 — No boot, o surr nasce antes do portal e monta as sessões em dobro

**Categoria:** Bugs e soluções

**Problema/Contexto:** Depois de um boot limpo, o menu da bandeja oferecia
"Autorizar colagem automática…" — como se a permissão nunca tivesse sido dada —
embora o ditado funcionasse e o texto colasse. O log do boot:

```
surr.portal: o portal reiniciou; refazendo as sessões
surr.shortcut: falha ao conectar sinais do portal: Activated= false Deactivated= false
surr.shortcut: atalho registrado; gatilho: "Pause"
surr.shortcut: atalho registrado; gatilho: "Pause"
surr.portal: sessão RemoteDesktop iniciada; teclado= true clipboard= true
surr.portal: EIS conectado
surr.portal: sessão RemoteDesktop iniciada; teclado= true clipboard= true
surr.portal: RemoteDesktop: "O compositor recusou a conexão de entrada (EIS): Session is already connected"
surr.portal: sessão de colagem indisponível: ...
```

O `QDBusServiceWatcher` do portal está em `WatchForOwnerChange`, e essa vigília
dispara **também quando o serviço aparece pela primeira vez**. No boot é
exatamente o que acontece: o autostart sobe o surr antes do
xdg-desktop-portal (pid 3027 contra 3052, no mesmo segundo), e a estreia do
`org.freedesktop.portal.Desktop` chega indistinguível de um reinício. O surr
então refazia as sessões por cima das que estavam nascendo — duas de
GlobalShortcuts e duas de RemoteDesktop. O compositor aceita uma conexão EIS
por sessão, recusou a segunda com *"Session is already connected"*, e o
`fail()` dela derrubou `active` e o rótulo da bandeja, enquanto a primeira
seguia viva e colando.

Reproduz no terminal, sem reiniciar o PC, bastando pôr o portal para nascer
depois:

```bash
systemctl --user stop app-br.com.cepify.Surr@autostart.service
systemctl --user stop plasma-xdg-desktop-portal-kde.service xdg-desktop-portal.service
surr          # a primeira chamada de portal reativa o serviço, e a estreia dispara o vigia
```

**Solução ou insight:** Um conserto de cada lado.

- O vigia guarda se já viu o portal com dono (`portalSeen`). Estreia — `oldOwner`
  vazio e nunca visto antes — não é reinício e não refaz nada.
- `RemoteDesktopSession::open()` ganhou a marca `opening`. `active` não servia de
  guarda porque só vira true no fim da cadeia (CreateSession → SelectDevices →
  RequestClipboard → Start → ConnectToEIS), que no boot medido levou 6 s — e os
  dois pedidos cabiam folgados nessa janela.

De brinde caiu o aviso `falha ao conectar sinais do portal`: era o segundo
`bind()` reassinando os mesmos sinais, e o Qt devolve `false` para assinatura
duplicada. Assinatura de sinal é match rule por nome de serviço, sobrevive ao
reinício do portal, e portanto se faz uma vez só.

Depois da correção, no mesmo cenário: um `atalho registrado`, uma `sessão
RemoteDesktop iniciada`, um `EIS conectado`, zero `Session is already
connected`, e a bandeja passando a dizer "Reconfigurar colagem automática…".

*Confirmado em boot de verdade (2026-08-31 17:50).* A corrida se repetiu com a
mesma folga do diagnóstico — surr às 17:50:12.275, `xdg-desktop-portal` às
17:50:12.523, 248 ms depois — e mesmo assim o log trouxe um `atalho
registrado`, uma `sessão RemoteDesktop iniciada`, um `EIS conectado`, e zero
`o portal reiniciou`, `falha ao conectar sinais`, `Session is already
connected` ou `sessão de colagem indisponível`. O boot anterior, com o mesmo
ordenamento, tinha dois de cada. Ler o rótulo da bandeja sem clicar nela:

```bash
# o :1.NN do daemon sai do RegisteredStatusNotifierItems, casando o pid
gdbus call --session --dest :1.61 --object-path /MenuBar \
  --method com.canonical.dbusmenu.GetLayout -- 0 -1 "['label']"
```

Cuidado com um falso alarme no log deste caminho: duas linhas de
`qt.qpa.services: Failed to register with host portal ... "Could not register
app ID: Connection already associated with an application ID"`. É o Qt
tentando registrar o app-id **depois** do `Registry.Register` do `main.cpp` e
sendo recusado — ou seja, o aviso é a prova de que o nosso registro chegou
primeiro. Preocupante seria o contrário.

**Por que importa:** O sintoma mentia nos dois sentidos — a bandeja negava uma
permissão que estava lá e funcionando. E nada disso aparece rodando `surr` na
mão, porque aí o portal já está de pé: é defeito exclusivo do boot, que é
justamente o único caminho por onde o programa realmente sobe.

---

### 2026-08-31 — A janela de configurações nunca gravava nada no disco

**Categoria:** Bugs e soluções

**Problema/Contexto:** A tela não tinha botão de salvar, o que sugeria
"aplica na hora". Não aplicava nunca. Nada do que fosse mudado ali chegava ao
`surrrc` — e o sintoma mais claro é que, depois de meses de uso,
`~/.config/surrrc` **não existia**.

Três coisas empilhadas:

1. `SettingsBackend::save()` existia, era `Q_INVOKABLE`, e **nenhum lugar do
   QML o chamava** (`grep -o "backend\.[A-Za-z_]*"` listava 25 chamadas; `save`
   não estava entre elas).
2. Sobraria o `KConfig` sincronizar no destrutor, mas `main.cpp` fazia
   `new surr::SettingsWindow` sem pai e nunca deletava — nenhum destrutor
   rodava.
3. E mesmo que gravasse, o daemon não veria: ele tem o `KConfig` dele, lido na
   partida, e não havia `reparseConfiguration` nem `KConfigWatcher` no projeto.
   Pior, o `shutdown()` dele chamava `save()` com a cópia velha em memória —
   despejaria o valor antigo por cima do novo.

Medido com o binário real, num `XDG_CONFIG_HOME` limpo e uma sonda temporária
que mudava um valor pelo caminho completo (QML → backend → `Settings` →
`KConfig`) e saía como se a janela fosse fechada:

```
qml: SONDA: minimumHoldMs agora e 999 - saindo
>>> surrrc NAO EXISTE — a alteracao SE PERDEU
```

**Solução ou insight:** Modelo OK/Aplicar/Cancelar, como os apps do Plasma.
O `SettingsBackend` ganhou um rascunho (`Draft draft` + `Draft stored`): o QML
escreve no rascunho e só o Aplicar empurra para o `Settings`, chama `save()`
(que é o `sync()` — sem ele o `writeEntry` fica só em memória) e avisa o daemon
por D-Bus em `/Control.reloadSettings`, que faz `reparseConfiguration` e
repassa os valores às peças. O `save()` do `shutdown()` saiu.

Duas decisões que parecem detalhe e não são:

- **"Sujo" é comparação (`draft != stored`), nunca uma marca de "mexeu".**
  É o que faz o botão Aplicar *apagar sozinho* quando o valor volta ao
  original. Sem isso ele deixa de servir para o que o usuário realmente usa:
  esbarrar num controle e enxergar que esbarrou.
- **O Aplicar empurra só os campos que mudaram.** `setAutostart` escreve (ou
  apaga) o `.desktop` do autostart; reescrever esse arquivo por causa de outra
  opção da mesma tela seria efeito colateral gratuito.

As chaves de API ficam fora do rascunho de propósito: têm botão próprio, vão
para o KWallet na hora, e o Cancelar não as desfaz.

**Por que importa:** O bug era invisível pelos dois lados. Pela interface,
"não tem botão de salvar" se lê como "salva sozinho". Pelo código, existe um
`save()` com cara de que alguém o chama. E o teste que pegaria isso não é ler o
código: é abrir, mudar, fechar e **olhar o arquivo**. Vale a regra: onde não
houver botão de salvar, verifique o disco, não a intenção do código.

---

### 2026-08-31 — A janela de configurações herdava o layer-shell pelo ambiente, não pelo código

**Categoria:** Bugs e soluções

**Problema/Contexto:** Aberta pela bandeja, a janela de configurações vinha em
tela cheia, sem borda e sem os botões de fechar/minimizar/maximizar. O código
parecia correto e continha a defesa explícita: `main.cpp` decide o modo antes
de criar o `QApplication` e só chama `LayerShellQt::Shell::useLayerShell()`
quando **não** é `--settings`; `ApplicationController::openSettings()` ainda
lança um processo separado, com o comentário dizendo exatamente por quê. Nada
disso adiantava.

A causa é que `useLayerShell()` **não é uma chamada de API** — é um
`qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell")`:

```
$ strings /usr/lib/x86_64-linux-gnu/libLayerShellQtInterface.so.6 | grep SHELL_INTEGRATION
QT_WAYLAND_SHELL_INTEGRATION
```

Ou seja, ela muda o **ambiente do daemon**, e o `QProcess::startDetached`
repassa esse ambiente ao filho. O processo de configurações pulava a chamada e
mesmo assim nascia com a variável posta:

```
$ tr '\0' '\n' < /proc/$(pgrep -x surr | tail -1)/environ | grep SHELL_INTEGRATION
QT_WAYLAND_SHELL_INTEGRATION=layer-shell
```

Rodar `surr --settings` direto do terminal **não reproduz** — ali a variável
não existe. Por isso o defeito só aparece no caminho real de uso.

**Solução ou insight:** `qunsetenv("QT_WAYLAND_SHELL_INTEGRATION")` no
`main.cpp`, no modo `--settings`, antes do `QApplication`. Fica fora do
`#ifdef SURR_HAS_LAYERSHELL` de propósito: a variável pode chegar pelo
ambiente mesmo num build sem LayerShellQt. Corrigir no lado que **recebe**
cobre todos os caminhos de lançamento; corrigir só no `startDetached` cobriria
um.

Verificação — o veredito é o protocolo Wayland, não a aparência:

```bash
env QT_WAYLAND_SHELL_INTEGRATION=layer-shell WAYLAND_DEBUG=1 \
    timeout 6 surr --settings 2>&1 |
  grep -oE 'zwlr_layer_surface_v1|xdg_toplevel' | sort | uniq -c
```

Antes: 9 × `zwlr_layer_surface_v1`. Depois: zero — só `xdg_toplevel`, que é
janela normal decorada. Cuidado ao ler: `zwlr_layer_shell_v1` continua
aparecendo 2 vezes porque é o `wl_registry.global` anunciando o protocolo;
quem denuncia a superfície criada é `zwlr_layer_surface_v1`.

Nota de lado: o compilador avisa que `useLayerShell()` está **deprecado desde
o LayerShellQt 6.6** — *"Calling useLayerShell is not needed since Qt 6.5"*.
Deixar de chamá-la eliminaria a variável de ambiente na origem, mas isso muda
como a cápsula obtém a superfície dela e não foi testado ponta a ponta.

**Por que importa:** É o modo de falha que nenhuma leitura de código pega. A
separação em dois processos existe justamente para isolar o layer-shell, o
comentário no `openSettings()` explica isso corretamente, e ainda assim o
isolamento vazava — porque o mecanismo real da biblioteca é ambiente herdado,
não estado de processo. Regra geral para este projeto: **antes de confiar que
um `QProcess` isola alguma coisa, verifique se a biblioteca não trabalha por
variável de ambiente** — `execve` copia o ambiente do pai, e nenhuma barreira
de código intercepta isso. E `/proc/<pid>/environ` mostra o ambiente do
`exec`, então ele nunca reflete um `qunsetenv` posterior: para conferir a
correção é preciso olhar o efeito (o protocolo), não a variável.


---

### 2026-09-01 — A chave salva na janela não chegava ao daemon, e o "Testar conexão" dizia OK

**Categoria:** Bugs e soluções

**Problema/Contexto:** Trocado o serviço para *Whisper próprio* e salva a chave,
o botão *Testar conexão* respondeu **"Conexão OK"** — e o ditado seguinte falhou
com *"A chave de Whisper próprio não está configurada."*

Três carimbos de tempo fecham o caso sozinhos:

```
daemon subiu ......  2026-08-31 17:50:12   ps -o lstart= -p 3027
surrrc gravado ....  2026-09-01 08:27:13   provider=cepify; o log traz "configurações recarregadas"
carteira gravada ..  2026-09-01 08:27:40   a chave, 27 s DEPOIS do reload
```

O daemon lê as chaves **uma vez**, no `readyChanged` do `SecretStore`, e as
entrega aos providers — que as guardam em cópia. O `reloadSettings` relia o
`surrrc` e reescolhia o provider ativo (por isso a bandeja já dizia "Serviço:
Whisper próprio"), mas nunca voltava à carteira. O `CepifyProvider` do daemon
seguia com a string vazia lida na véspera, e `isConfigured()` é
`hasApiKey() && endpoint.isValid()`.

O "Conexão OK" não mentia: o teste roda no processo `--settings`, que acabara de
gravar a chave e a relê na hora. Os dois processos discordavam, e cada um estava
certo sobre si mesmo.

Havia um segundo buraco no mesmo caminho: a chave fica **fora** do rascunho
(tem botão próprio), e `apply()`/`applyAndClose()` começam com
`if (!isDirty()) return;`. Quem salvasse só a chave e clicasse OK não dispararia
`reloadSettings` nenhum — nem com o lado do daemon já consertado.

**Solução ou insight:** Um conserto de cada lado, espelhando o que já se fez com
a configuração:

- `ApplicationController::applySecrets()`, irmã da `applyLiveSettings()`, junta
  num lugar só o que empurra as chaves para os providers; o `reloadSettings`
  chama-a antes do `selectActiveProvider()`, que é quem recalcula a bandeja.
- Salvar ou apagar uma chave na janela agora avisa o daemon, pelo fecho comum
  `secretChanged()`. Ali a janela continua aberta, então disparar e esquecer
  basta — é o OK que precisa esperar o watcher (ver *Sair do processo descarta a
  mensagem D-Bus assíncrona*).
- `SecretStore::refresh()`: no KWallet não faz nada, porque cada `read()` já é
  uma ida ao kwalletd; quem precisava era o refúgio em disco (`secrets.json`),
  lido uma vez só no `open()` — e quem grava as chaves é outro processo. Sem
  ele, o conserto valeria só para quem tem KWallet.

Depois: daemon reiniciado pela unidade do autostart, bandeja com "Pronto" e
"Serviço: Whisper próprio" — com a chave faltando, `selectActiveProvider()` poria
"Configuração pendente" —, e `surr-probe-transcribe <wav> cepify` voltando
**HTTP 200** do servidor real, com a chave lida da carteira.

Observação medida de lado, deixada como está por causa do congelamento: o
`cycleAborted` põe a bandeja em `Idle` incondicionalmente. Foi por isso que ela
dizia "Pronto" mesmo sem chave — o primeiro ditado com erro apagou o
"Configuração pendente" que o `selectActiveProvider()` tinha acabado de pôr.

**Por que importa:** O defeito é, de novo, *uma cópia em memória que ninguém
avisou* — e o `CLAUDE.md` já trazia a regra: "valor novo que alguma peça guarde
em cópia entra no `applyLiveSettings()`". Só que ela foi escrita pensando em
configuração, e a chave não é configuração: mora em outro armazém, tem outro
botão, e escapou justamente da regra que existia para pegá-la. **A lista de
"quem guarda cópia" é por cópia, não por arquivo** — se o valor atravessa os dois
processos, tanto faz se ele veio do `surrrc` ou do KWallet.

E o diagnóstico tinha um agravante: o teste verde. **Um teste que roda no
processo errado não mede o que o usuário vai usar, mede o processo que o
executa.** Vale para o *Testar conexão* e para qualquer botão de autodiagnóstico
numa arquitetura de dois processos.

---

### 2026-10-28 — O microfone do fone Bluetooth sumia da lista: culpa do backend PipeWire do Qt

**Categoria:** Bugs e soluções

**Problema/Contexto:** Num Kubuntu 26.04 igual a este, o fone Bluetooth de um
usuário não era reconhecido como microfone. A suspeita inicial ("o surr só
aceita USB e P2") não se sustenta: o `AudioEngine` não filtra nada, usa o que o
`QMediaDevices` lista. Quem filtrava era o próprio Qt.

O WirePlumber 0.5.13 dá a todo fone Bluetooth um microfone que existe mesmo em
A2DP (perfil sem microfone): um loopback `bluez_input.*`, e quando alguém grava
dele, o fone troca para HFP em 500 ms. Só que o backend **PipeWire nativo** do
Qt 6.10 — o padrão, sem aviso — só lista um nó depois de ler os formatos dele,
e esse loopback sem nada ligado atrás nunca responde. O fone some da lista;
sem gravar dele, a troca para HFP nunca acontece. Um ovo e galinha.

Reproduzido sem fone, com um `pw-loopback` igual ao do `bluez.lua` (comando e
saída em `docs/ACHADOS-DO-AMBIENTE.md` §15): o log
`qt.multimedia.pipewire.registry` mostra o nó chegando, e a lista não o traz.

**Solução ou insight:** `AudioRecorder::selectAudioBackend()` põe
`QT_AUDIO_BACKEND=pulseaudio` antes do `QApplication` — em todos os modos,
porque a janela de configurações também lista microfones. O PulseAudio é
servido pelo `pipewire-pulse`, lista o loopback, e os ids (`node.name`) são os
mesmos, então o microfone salvo de quem já usava continua valendo. Quem já
definiu a variável no ambiente decide. Verificado: a lista passa a trazer o
fone simulado, a gravação sai dele por um `Stream/Input/Audio` (o que o
`autoswitch-bluetooth-profile.lua` vigia), e o `--settings` aberto pela
bandeja herda `QT_AUDIO_BACKEND=pulseaudio`.

Não verificado, por falta de fone: a troca A2DP→HFP de verdade e quanto do
começo da fala ela come.

**Por que importa:** Não há erro em lugar nenhum — o dispositivo
simplesmente não existe para o app. E não aparece em nenhuma máquina sem fone
Bluetooth, como as duas em que o surr foi desenvolvido. Para imitar um
dispositivo que não se tem, monte o nó com as mesmas propriedades que o
WirePlumber monta (o script está em `/usr/share/wireplumber/scripts/`) e
compare os dois backends com `QT_AUDIO_BACKEND`.

---

## Pegadinhas de ambiente, dependências ou bibliotecas

### 2026-08-31 — Reinício do portal apaga o app-id registrado; do terminal ele vira `org.kde.konsole`

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** Testando um reinício do portal com o surr lançado **do
terminal**, o KDE abriu dois diálogos dizendo *"Konsole deseja registrar o
seguinte atalho"* e *"Konsole está solicitando privilégios especiais"*, e o
`Start` voltou recusado. O log do portal mostra a identidade trocando no meio
do caminho:

```
xdp-kde-GlobalShortcuts:  app_id: "br.com.cepify.Surr"   (antes do reinício)
xdp-kde-GlobalShortcuts:  app_id: "org.kde.konsole"      (depois)
```

O `Registry.Register` do `main.cpp` amarra o app-id à conexão **dentro do
processo do portal**. O portal reiniciado não sabe mais quem somos e cai na
dedução por cgroup — que, para um processo lançado do terminal, é o terminal.
Sob a unidade do autostart a dedução acerta: testado o mesmo reinício com
`systemctl --user restart app-br.com.cepify.Surr@autostart.service`, o app_id
seguiu `br.com.cepify.Surr`, sem diálogo nenhum e restaurando em silêncio.

O estrago fica gravado sob o nome errado. Para limpar:

```bash
kwriteconfig6 --file kglobalshortcutsrc --group org.kde.konsole --key dictate --delete

# permissões de entrada concedidas ao app-id errado:
dbus-send --session --print-reply --dest=org.freedesktop.impl.portal.PermissionStore \
  /org/freedesktop/impl/portal/PermissionStore \
  org.freedesktop.impl.portal.PermissionStore.List string:"remote-desktop"
# ...Lookup em cada id para ver o app-id, e Delete nos que não são br.com.cepify.Surr
```

**Solução ou insight:** Teste de portal se faz com o daemon na unidade do
autostart, não com `surr` solto no terminal. O terminal serve para o cenário de
boot (portal parado, surr antes) — ali nada reinicia no meio e o app-id
registrado na partida vale a sessão inteira.

**Por que importa:** Já tinha acontecido de a permissão parar em
`org.kde.konsole` por rodar de `build/` sem instalar (está no `CLAUDE.md`). O
mecanismo agora é outro e o binário instalado não protege: basta o portal
reiniciar. O efeito é o mesmo — `kglobalshortcutsrc` e o armazém de permissões
guardam concessões sob o nome de quem lançou o processo, e ninguém percebe até
um diálogo aparecer com o nome errado.

---

### 2026-08-31 — Sair do processo descarta a mensagem D-Bus assíncrona

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** Ao ligar o Aplicar/OK das configurações ao
`reloadSettings` do daemon, o botão OK gravava o arquivo e **o daemon não
recebia nada**. O código era o mesmo padrão do `configureShortcut`, que
funciona:

```cpp
QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
```

A diferença não estava na chamada, e sim no que vinha depois: o OK encerra o
processo em seguida. `NoBlock` (e o `asyncCall`) só enfileiram a mensagem; quem
a escreve no socket é a volta ao event loop. Saindo na mesma linha, ela morre
com o processo. O `configureShortcut` nunca sofreu disso porque a janela
continua aberta.

Medido, com o daemon logando cada reload: botão Aplicar (janela segue aberta) →
o daemon vê o valor novo; botão OK (`apply()` + saída imediata) → nenhum reload
no log, mas o `surrrc` gravado. Grave só metade, e o sintoma vira "às vezes
pega, às vezes não".

**Solução ou insight:** Quando a saída vem logo depois, use `asyncCall` e espere
o `QDBusPendingCallWatcher` antes de sair — não é event loop aninhado, é o loop
normal continuando a girar até o sinal chegar. Com um `QTimer::singleShot` de
rede, para a janela não ficar presa se nada voltar.

No caminho oposto, quando a janela **fica** aberta, o disparo e esquecer basta.

Junto veio outra: `Qt.quit()` no QML **não fecha** uma `QQuickView`. Ele emite
`QQmlEngine::quit`, e a ligação com o `QCoreApplication` só é automática no
`QQmlApplicationEngine`. Com a `QQuickView` é preciso conectar à mão:

```cpp
connect(m_view->engine(), &QQmlEngine::quit, qApp, &QCoreApplication::quit);
```

**Por que importa:** As duas falham em silêncio e do mesmo jeito — o clique
"funciona", nada aparece no log, e o efeito simplesmente não acontece. E as
duas só se manifestam no botão que fecha a janela, que é justamente o caminho
que ninguém testa por último.

---

### 2026-08-31 — O filho de um `ScrollView` precisa de `availableWidth`, não de `parent.width`

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** Na janela de configurações, os botões da borda direita
(*Apagar*, *Alterar atalho...*, *Autorizar...*) encostavam na barra de
rolagem, apesar de o layout ter `Layout.rightMargin: root.pad` (18px) em todos
os itens. A margem existia; o que faltava era espaço para ela.

O `Settings.qml` fazia `ColumnLayout { width: root.width }` dentro do
`ScrollView`. Medido em execução, com a janela no tamanho padrão:

```
width= 640   availableWidth= 619   rightPadding= 21   vbarW= 21
```

O `ScrollView` põe a largura da barra vertical no próprio `rightPadding`, e é
isso que separa `width` de `availableWidth`. Amarrando a coluna a `root.width`
ela ia até 640, os últimos 21px ficavam debaixo da barra, e a folga real na
direita virava 18 − 21 = **−3px**.

**Solução ou insight:** Dentro de um `ScrollView`, o filho se amarra a
`availableWidth`, nunca à largura da janela:

```qml
ScrollView {
    id: scroller
    contentWidth: availableWidth
    ColumnLayout { width: scroller.availableWidth }
}
```

Note que `contentWidth: availableWidth` **já estava lá** e não resolvia: ele
governa o `Flickable`, não a largura do item filho — o QQC2 não amarra o filho
sozinho. São duas ligações distintas, e é fácil ver a primeira e achar que a
segunda veio junto.

**Por que importa:** O sintoma parece "faltou margem" e leva a mexer no
`pad` — que só piora, porque o problema é a coluna larga demais, não a margem
estreita demais. E a medição é o jeito de decidir: se `rightPadding` for igual
à largura da barra, o desconto existe e o conserto é trocar a ligação; se for
0, o estilo não reserva espaço e seria preciso descontar a barra à mão.

---

### 2026-08-31 — O teste `settings` não roda sem display; os outros rodam

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** O `README.md` e o `src/CMakeLists.txt` afirmam que a
suíte do ctest é headless. Isso vale para os seis testes que linkam só
`surr_core`, mas **não** para `settings`, que linka `surr_platform`. Sem
display de verdade ele aborta antes do primeiro caso:

```
$ env -u DISPLAY -u WAYLAND_DISPLAY XDG_RUNTIME_DIR=/tmp/vazio ./build/bin/settings
This application failed to start because no Qt platform plugin could be initialized.
```

O mesmo comando em `./build/bin/statemachine` passa os 16 casos. A causa é o
`QTEST_MAIN`, que escolhe a classe de aplicação pelas macros de compilação:
`surr_platform` linka `Qt6::Gui` como PUBLIC, o CMake propaga `-DQT_GUI_LIB`
para o objeto do teste, e o `QTEST_MAIN` instancia um `QGuiApplication` em vez
de `QCoreApplication`.

Numa sessão Plasma normal isso passa despercebido: mesmo com `DISPLAY` e
`WAYLAND_DISPLAY` apagados, o plugin wayland acha o socket sozinho pelo
`XDG_RUNTIME_DIR`. Só um ambiente de verdade sem sessão gráfica expõe a falha.

**Solução ou insight:** Em máquina sem sessão gráfica (CI, contêiner, sessão
SSH), rode com `QT_QPA_PLATFORM=offscreen` — verificado, passa os 6 casos:

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

A regra geral: **um teste é headless até linkar algo que puxe `Qt6::Gui`.**
Teste novo que precise ser headless de verdade tem de ficar em `surr_core`.

**Por que importa:** Botar a suíte em CI sem isso dá uma falha que não
reproduz na máquina de ninguém. E é o primeiro sinal de que a fronteira das
camadas vazou: se um teste começou a precisar de `surr_platform`, vale
perguntar se a lógica testada não deveria estar em `surr_core`.

---

### 2026-08-31 — `appstreamtest` valida o metainfo INSTALADO, não o do repositório

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** O `ctest` roda 8 testes, mas só 7 são deste projeto. O
`appstreamtest` vem de graça do `KDECMakeSettings` do ECM
(`/usr/share/ECM/kde-modules/appstreamtest.cmake`), e ele não olha para
`data/br.com.cepify.Surr.metainfo.xml`: ele lê `build/install_manifest.txt`,
filtra os caminhos terminados em `.metainfo.xml` e valida **os arquivos já
instalados** — neste ambiente, `~/.local/share/metainfo/`.

Duas consequências:

1. Num clone limpo, antes do primeiro `cmake --install`, o manifesto não
   existe: o script imprime `Not installed yet, skipping` e o teste passa sem
   validar nada.
2. Depois de instalar, editar o metainfo no repositório e rodar `ctest` valida
   a **cópia velha**. O teste passa (ou falha) pelo arquivo errado.

**Solução ou insight:** Ao mexer no metainfo, valide o fonte à mão e só confie
no `ctest` depois de reinstalar:

```bash
appstreamcli validate --no-net data/br.com.cepify.Surr.metainfo.xml
cmake --install build && ctest --test-dir build -R appstreamtest
```

**Por que importa:** Evita as duas leituras erradas: "o teste passou, o
metainfo está certo" (quando nada foi validado) e "quebrei o metainfo" (quando
o inválido é o instalado). Vale para qualquer teste que o ECM injeta sozinho —
conferir a origem antes de depurar um teste que você não escreveu.

---

### 2026-08-31 — O separador de dígitos do C++ quebra varredores de código

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** Ao escrever o check `log-limpo` do `verificar.sh` — um
varredor que remove strings e comentários antes de procurar segredos nos
`qCDebug` — ele voltou verde num arquivo onde o vazamento tinha sido plantado
de propósito. O varredor não estava achando **nenhuma** das três chamadas de
log do `OpenAiCompatibleProvider.cpp`.

A causa não era a regex: era `constexpr int kDefaultTimeoutMs = 90'000;` na
linha 20. O varredor tratava `'` como início de literal de caractere e engolia
tudo até a próxima aspa simples — no caso, a da linha 128 (`, 'f', 1`). Cento e
oito linhas do arquivo, incluindo os logs, sumiam antes da análise.

**Solução ou insight:** `'` só abre literal de caractere quando o caractere
anterior **não** é alfanumérico nem `_`. Com essa guarda, `90'000` é separador
de dígito e `'f'` continua sendo literal:

```python
elif c == "'" and not (i and (txt[i - 1].isalnum() or txt[i - 1] == "_")):
```

A falha foi detectada porque o check foi testado nos **dois** sentidos: verde no
código real e vermelho numa cópia com o defeito plantado. Só o segundo teste a
revelou — o primeiro passava por motivo errado.

**Por que importa:** Vale para qualquer ferramenta que varra este código
(linter caseiro, contador, extrator de strings): o projeto usa separador de
dígitos, e um varredor ingênuo falha **em silêncio**, dando verde por não ter
lido nada. E é o argumento geral: um check que nunca foi visto ficar vermelho
não é um check, é decoração.

---

### 2026-08-31 — `PIPESTATUS` é do bash; no zsh o veredito do `verificar.sh` some

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** O contrato do `ACEITACAO.md` diz que "pronto" é o
**código de saída** do `verificar.sh`. Numa rodada de verificação, o script foi
executado assim, para ver a saída e guardar o log ao mesmo tempo:

```
$ ./verificar.sh 2>&1 | tee rodada.txt; echo "rc=${PIPESTATUS[0]}"
[...]
PASSOU — 12 de 12 checks
rc=
```

Os 12 checks passaram, mas o `rc` voltou **vazio**. O shell padrão desta máquina
é o zsh, e no zsh o array se chama `pipestatus` (minúsculo); `PIPESTATUS` não
existe e, sem `set -u`, expande para nada. O `$?` puro também não serviria: num
pipe ele é o status do `tee`, que é sempre 0.

**Solução ou insight:** Numa rodada de verificação, **não canalize o
`verificar.sh`**. Redirecione para arquivo e leia o `$?` da própria chamada:

```bash
./verificar.sh > rodada.txt 2>&1
rc=$?
cat rodada.txt
echo "rc=$rc"
```

O script já imprime `PASSOU`/`FALHOU` na última linha, mas o texto é
confirmação — quem decide é o `rc`.

**Por que importa:** É a única forma de a mecânica do contrato falhar em
silêncio. Um `rc` vazio se parece com sucesso: ninguém vê `✘`, a última linha
diz `PASSOU`, e a rodada seria dada por boa sem nunca ter lido o veredito. O
projeto inteiro trocou "opinião" por "código de saída" justamente para não
depender de leitura — perder esse número no pipe desfaz a troca.

---

### 2026-10-28 — Mover a pasta do projeto inutiliza o `build/`

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** Depois que o repositório mudou de
`~/desenvolvimento/temporarios/surr` para `~/desenvolvimento/projetos/cpp/surr`,
o `cmake --build build` falhou com `ninja: error: rebuilding 'build.ninja':
subcommand failed`. O `CMakeCache.txt` guarda caminhos absolutos da fonte e do
próprio build, e o `--regenerate-during-build` tentava usar o caminho antigo.

**Solução ou insight:** `build/` é só artefato: `rm -rf build` e reconfigurar
com o comando do `CLAUDE.md` (o `CMAKE_INSTALL_PREFIX` em `~/.local` precisa ir
junto, senão o `cmake --install` vai para `/usr/local`).

**Por que importa:** O erro não menciona o caminho antigo na primeira linha, e
parece defeito do `CMakeLists.txt`. Não é: é cache de outra pasta.

---

### 2026-10-28 — Capturas para o README: cápsula pelo `--preview`, janela pelo `spectacle -a`

**Categoria:** Pegadinhas de ambiente, dependências ou bibliotecas

**Problema/Contexto:** As imagens de `docs/assets/` precisavam mostrar a cápsula
e a janela reais, sem expor o resto da tela nem nenhum endereço ou chave.

**Solução ou insight:**

- Cápsula: `surr --preview` em segundo plano e `spectacle -b -n -f -o s.png` a
  cada ~1 s. A tela inteira (3 monitores, 5760×1080) fica fora do repositório;
  entra só o recorte de 322×60 com máscara arredondada
  (`magick ... -compose CopyOpacity`), em fundo transparente.
- Janela: `surr --settings` e `spectacle -b -n -a -e` (janela ativa, sem
  decoração). Com o serviço em Groq, o campo *Endereço* nem aparece.
- `inkscape arquivo.svg -o arquivo.png` serve para conferir os SVG antes do
  GitHub. Foi assim que apareceu um gradiente sumindo: numa linha reta a caixa
  tem altura zero, e o gradiente em `objectBoundingBox` não é desenhado. A
  saída é `gradientUnits="userSpaceOnUse"`.

**Por que importa:** No Wayland nenhum programa consegue capturar a tela "por
fora" sem o portal, mas o `spectacle` é do próprio KDE e captura direto.
E o recorte rente é o que impede vazar o conteúdo dos outros monitores.

---

## Decisões de arquitetura

### 2026-08-31 — Quem derruba a sessão do portal tem de ser um caminho só

**Categoria:** Decisões de arquitetura

**Problema/Contexto:** Há dois detectores para a morte da sessão de colagem: o
vigia do portal (reiniciou → refaz) e o `EisInjector::disconnected` (o descritor
morreu → refaz, com espera de 2 s, 4 s, 8 s e teto de três tentativas). Enquanto
o vigia desistia sozinho — o `open()` dele voltava cedo porque `active` ficava
preso em true na sessão morta —, os dois nunca se encontravam. Ao consertar isso
(o vigia passou a fechar antes de reabrir), eles começaram a se atropelar: o
vigia refazia a sessão em 1,5 s, a tentativa do EIS acordava em 2 s e **fechava
a sessão boa** para abrir outra, e cada derrubada alimentava a tentativa
seguinte. Um reinício do portal virou quatro sessões em 14 s, terminando em

```
surr.inject: EIS caiu 3 vezes; desistindo da colagem automática
```

— notificação de erro na cara do usuário, com a colagem voltando a funcionar no
segundo seguinte.

**Solução ou insight:** O `close()` saiu da tentativa adiada e foi para o
instante da desconexão, que é onde a informação existe: o descritor morreu, logo
a sessão morreu. A parte adiada virou só `open()`, e a guarda `active || opening`
decide — se outro caminho já refez, o pedido vira o silêncio que deve ser.
A regra: **derrubar tem um dono só; reabrir pode ter vários, desde que todos
passem pela mesma guarda.**

**Por que importa:** É o preço de ter dois detectores para a mesma falha, e o
conflito não aparece nem no caminho feliz nem no de erro — só reiniciando o
portal com o daemon vivo e **contando as sessões** no log. Dois consertos
corretos, cada um defensável sozinho, produziram um defeito novo; e ele se
manifesta como erro visível ao usuário justamente quando tudo, no fundo,
funcionou.

---

### 2026-08-31 — O estado da cápsula atravessa a fronteira QML↔C++ como inteiro cru

**Categoria:** Decisões de arquitetura

**Problema/Contexto:** `OverlayController` empurra o estado para o QML com
`rootObject()->setProperty("state", static_cast<int>(state))`, e
`src/qml/Overlay.qml` declara `property int state: 0` com a tabela do
mapeamento num comentário (`0 Hidden · 1 Preparing · 2 Recording · 3
Transcribing · 4 Success · 5 Error`). Não há `Q_ENUM` registrado no contexto
QML, nem objeto de contexto: a `QQuickView` da cápsula é criada em C++ sem
`setContextProperty`, de propósito, para manter mínima a interface entre as
camadas.

**Solução ou insight:** É acoplamento deliberado, e a compensação é que
`OverlayController::State` e o `Overlay.qml` **mudam sempre juntos**.
Reordenar ou inserir um valor no meio do enum não gera erro de compilação nem
aviso de runtime — a cápsula simplesmente mostra o estado errado, e o
`--preview` é a forma rápida de conferir:

```bash
surr --preview     # percorre preparando → gravando → transcrevendo → pronto
```

Valor novo entra **no fim** do enum, nunca no meio.

**Por que importa:** É a única fronteira do projeto sem verificação do
compilador — em todo o resto, a separação das camadas é imposta pelo linker
(`surr_core` não linka `Qt6::DBus`, então um include errado vira erro de
compilação). Saber onde a rede de proteção termina é o que decide onde olhar
quando a cápsula "fica esquisita" sem nenhum log de erro.

*Atualização:* essa fronteira deixou de ser não verificada. O check `qml-enum`
do `verificar.sh` compara nome, ordem e índice dos dois lados e exige que o
`Overlay.qml` trate todos os estados.

---

### 2026-08-31 — O veredito de "pronto" é o código de saída do `verificar.sh`

**Categoria:** Decisões de arquitetura

**Problema/Contexto:** "Analise e veja se está tudo certo" é ordem de busca, não
teste: sempre sobra alguma coisa, e cada sessão limpa reencontra o que a sessão
anterior já tinha considerado e aceito. Auditoria sem critério determinístico
não termina.

**Solução ou insight:** O contrato ficou em `ACEITACAO.md` e o juiz em
`./verificar.sh` — 12 checks, sai 0 só se todos passarem. As regras que
importam:

- **Só entra check que a máquina decide sozinha.** Nada de rede: um check que
  depende do proxy ou da Groq daria "FALHOU" sem o projeto ter defeito.
- **Cada check tem uma resposta para "o que eu quebraria para deixá-lo
  vermelho?"** Sem essa resposta, o check é decoração e sai da lista.
- **O que depende de humano vai para "Só eu julgo"** e é marcado **uma vez** —
  é a memória que a sessão limpa não tem. O ditado ponta a ponta mora ali:
  no Wayland não dá para simular a tecla Pause.
- **"NÃO faz parte" e "Limitações aceitas" são definitivos.** Nada listado
  neles volta a ser relatado como achado.

Três checks cobrem promessas que nenhum teste unitário alcançava: `sem-x11`
(o ferramental de X11 falha em silêncio e é o que uma sessão futura mais tende
a reintroduzir "para consertar a colagem"), `sem-bloqueio` (event loop aninhado
reordena `Activated`/`Deactivated` e quebra o push-to-talk) e `log-limpo`
(chave, token ou texto ditado num `qCDebug`).

**Por que importa:** Tira o veredito da opinião e põe num `rc`. "Pronto" deixa
de ser "ninguém acha mais nada", que é infalsificável, e passa a ser
"`./verificar.sh` sai 0". Sem isso, cada rodada de revisão mexe em código que
já funciona — e o risco de regressão passa a ser maior que o de defeito
remanescente. Protocolo completo em
`~/desenvolvimento/documentacao/PROMPT_DE_VERIFICACAO.md`.

---

### 2026-08-31 — O projeto está CONGELADO: auditoria nova só com defeito real de uso

**Categoria:** Decisões de arquitetura

**Problema/Contexto:** O `ACEITACAO.md` termina com a linha

```
CONGELADO em d645df9 — 2026-08-31. Só mexer com pedido explícito meu ou defeito
real de uso.
```

Ela foi escrita depois de uma rodada de verificação em ambiente limpo (sem
`DISPLAY`, sem `WAYLAND_DISPLAY`, sem variáveis de proxy) em que o
`./verificar.sh` saiu 0 nos 12 checks e nenhum P0 foi reproduzido — e depois de
o ditado ponta a ponta já estar marcado em "Só eu julgo".

**Solução ou insight:** Sessão nova neste repositório **não abre auditoria por
conta própria.** Nada de "analise e veja se está tudo certo" espontâneo, nada de
varrer o código atrás de melhoria. O que vale:

- pedido explícito do Daniel — aí faça só o que ele pediu;
- defeito real observado no uso — aí o defeito é o ponto de partida, não uma
  varredura;
- mudança de código de verdade — aí rode o `./verificar.sh` **inteiro** e cole a
  saída, sem canalizar o script — no zsh o `PIPESTATUS` não existe e o
  veredito some (ver a entrada dele em *Pegadinhas*).

Descongelar é decisão do Daniel, e apagar a linha do `ACEITACAO.md` faz parte
dela.

**Por que importa:** Este é o passo que fecha o loop que o
`PROMPT_DE_VERIFICACAO.md` existe para matar. A partir daqui, cada rodada extra
de revisão mexe em código que hoje passa: o risco de regressão ficou **maior**
que o de defeito remanescente. Uma sessão limpa não tem como saber disso lendo o
código — o código parece tão auditável quanto no primeiro dia. Por isso a nota
mora aqui, que entra no contexto sozinho, e não só no `ACEITACAO.md`, que alguém
precisa lembrar de abrir.

---

### 2026-10-28 — O servidor Whisper próprio não tem endereço padrão no código

**Categoria:** Decisões de arquitetura

**Problema/Contexto:** O repositório ia virar público, e o endereço do servidor
Whisper próprio não podia aparecer nele. Ele estava como padrão em dois lugares
(`CepifyProvider::defaultBaseUrl()` e o `SURR_GET` de `cepifyBaseUrl`), nos
testes, na documentação e em mensagens de commit antigas.

**Solução ou insight:** O padrão passou a ser vazio. Sem base, o endpoint fica
inválido e `isConfigured()` devolve false, então a bandeja mostra
"Configuração pendente" até o usuário digitar o endereço. O
`surr-probe-transcribe` passou a ler o endereço do `surrrc`
(`Settings().cepifyBaseUrl()`), porque dependia do padrão. Nesta máquina o
endereço foi gravado no `surrrc` com `kwriteconfig6`, para o daemon seguir igual.
Os testes usam `whisper.exemplo.com.br`.

A regra do `CLAUDE.md` ("toda chave nova precisa de um padrão utilizável")
continua valendo: o padrão utilizável é o Groq, que é o serviço padrão.

**Por que importa:** Tirar dos arquivos não basta para um repositório público:
o histórico do Git guarda cada versão antiga. Qualquer valor que não pode ficar
público tem de sair também do histórico (e das mensagens de commit), antes de
publicar.
