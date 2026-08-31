# surr

Ditado por voz push-to-talk para **KDE Plasma 6 no Wayland**.

Segure a tecla, fale, solte: a transcrição aparece onde o cursor já estava.

```
Pause ↓ → grava → Pause ↑ → transcreve → copia → cola no cursor
```

Nativo em C++20/Qt 6/KF6. Sem Electron, sem daemon em Python, sem `xdotool`,
sem XTest, sem `/dev/uinput`, sem root.

---

## Como funciona

| Peça | Mecanismo |
|---|---|
| Atalho global | `org.freedesktop.portal.GlobalShortcuts` v2 — `Activated`/`Deactivated` dão press **e** release |
| Captura | `QAudioSource` (Qt Multimedia → PipeWire), WAV PCM próprio |
| Transcrição | Qt Network, multipart, direto na API — Groq ou servidor Whisper próprio |
| Área de transferência | `org.freedesktop.portal.Clipboard`, atada à sessão RemoteDesktop |
| Colagem | `RemoteDesktop.ConnectToEIS` + **libei**, só `Ctrl+V` |
| Credenciais | KWallet (KF6) |
| Bandeja | `KStatusNotifierItem` |
| Cápsula | Qt Quick + **LayerShellQt**, sem foco de teclado |

### Por que o portal em tudo

No Wayland o compositor é o dono do teclado e da área de transferência. Três
consequências que definem a arquitetura:

1. **Só o portal dá press *e* release.** Um `QAction` comum dispara uma vez; sem
   o release não existe "segure e fale".
2. **Um cliente Wayland sem foco não consegue definir a seleção.**
   `wl_data_device.set_selection` exige um serial de input de uma superfície com
   foco. A cápsula do `surr` é layer-shell sem foco nenhum, então
   `QClipboard::setText()` seria descartado **em silêncio** e o `Ctrl+V` colaria
   o conteúdo anterior. O portal não tem essa exigência.
3. **XTest é descartado sem erro.** O Xwayland do Plasma 6 roda com
   `-enable-ei-portal`: a injeção é desviada para o portal e, sem permissão,
   simplesmente não acontece. Nenhuma exceção, nenhum código de erro.

O texto inteiro vai para a área de transferência e só então é emitido `Ctrl+V`.
Digitar caractere a caractere traria de volta todos os problemas de Unicode,
layout ABNT2, dead keys e acentos que a colagem resolve de graça.

---

## Instalação

### Dependências (Kubuntu 26.04)

```bash
sudo apt install -y \
  ninja-build extra-cmake-modules \
  qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-dev-tools \
  qt6-multimedia-dev qt6-tools-dev qt6-svg-dev qt6-wayland-dev qt6-shadertools-dev \
  libkf6config-dev libkf6coreaddons-dev libkf6wallet-dev libkf6statusnotifieritem-dev \
  libkf6notifications-dev libkf6i18n-dev libkf6iconthemes-dev libkf6kcmutils-dev \
  libkf6globalaccel-dev libkf6dbusaddons-dev libkf6windowsystem-dev libkf6crash-dev \
  libkf6guiaddons-dev libkf6colorscheme-dev libkf6kio-dev libkirigami-dev \
  liblayershellqtinterface-dev libei-dev libxkbcommon-dev
```

Nomes que **não** existem no 26.04 e costumam aparecer em guias antigos:
`libqt6svg6-dev` (é `qt6-svg-dev`), `kf6-kirigami-dev` (é `libkirigami-dev`),
`qt6-layer-shell-dev` (é `liblayershellqtinterface-dev`), `libportal-qt6-dev`
(não existe para Qt6 — o portal é falado direto por `Qt6::DBus`).

### Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Instalação

```bash
sudo cmake --install build            # /usr/local
```

ou, sem root:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build && cmake --install build
```

> **Instalar não é opcional.** O `xdg-desktop-portal` resolve o app-id com
> `g_desktop_app_info_new()`, que devolve `NULL` quando o `.desktop` não existe
> **ou** quando o binário do `Exec` não está no `PATH` — e o `PATH` do portal
> (serviço systemd de usuário) não inclui `~/.local/bin`. Por isso o `.desktop`
> é gerado pelo CMake com o caminho **absoluto** do binário. Rodando direto da
> pasta de build, o `Registry.Register` falha e a permissão de colagem acaba
> guardada sob o app-id de quem lançou o processo.

---

## Primeira execução

```bash
surr            # sobe na bandeja, sem abrir janela
```

1. **Ícone na bandeja → Configurações…**
2. Escolha o serviço e cadastre a chave (vai para o KWallet).
3. **Autorizar colagem automática…** — aparece um diálogo do KDE; aprove uma vez.
   A permissão fica guardada (`persist_mode=2`) e não volta a perguntar.
4. Opcional: **Iniciar com a sessão**.

O atalho já vem sugerido como **Pause/Break** e é registrado sozinho.

---

## Configurar cada provider

### Groq

* Endpoint: `https://api.groq.com/openai/v1/audio/transcriptions`
* Modelos: `whisper-large-v3-turbo` (padrão, rápido) ou `whisper-large-v3`
* Limite: 25 MB (gratuito) / 100 MB (pago)

Cadastre a chave em **Configurações → Credenciais → Chave Groq**.

**Precedência da chave**, nesta ordem:

1. variável de ambiente `GROQ_API_KEY` — conveniência de desenvolvimento;
2. KWallet.

O ambiente vence para dar como testar outra chave sem mexer na carteira.
A variável **nunca** é gravada em disco. Quando ela está definida, a interface
mostra o campo desabilitado, dizendo de onde a chave veio.

Se você usava o **Sussurro** antes, o `surr` detecta a chave em texto puro em
`~/.config/sussurro/config.json` e oferece importá-la para o KWallet. O arquivo
original não é alterado.

### Servidor Whisper próprio

Conforme `whisper-turbo-api/USO.md`:

* Endereço: `https://whisper.exemplo.com.br` (configurável)
* Caminho montado: `<base>/v1/audio/transcriptions`
* Autenticação: `Authorization: Bearer <chave>`
* Modelo: `whisper-large-v3-turbo`
* Extra: campo `hotwords` para vocabulário de domínio
* Limites: 100 MB, 60 requisições por minuto
* Saúde: `GET <base>/health`

O endereço é normalizado: aceita sem esquema, com barra sobrando e com `/v1`
colado do `USO.md` — nada disso vira `/v1/v1/...`.

> **User-Agent é obrigatório.** A proteção anti-bot da Cloudflare na frente
> desse serviço casa qualquer `User-Agent` começando com `OpenAI/` e devolve
> 403 antes de chegar na origem. O `surr` envia `surr/<versão>`.

---

## Como funciona a permissão de colagem

```
Registry.Register(app_id)          ← primeira chamada ao portal, uma só vez
RemoteDesktop.CreateSession
  → SelectDevices(types=KEYBOARD, persist_mode=2, restore_token?)
  → RequestClipboard                ← precisa vir ANTES do Start
  → Start                           ← o diálogo aparece aqui, uma vez
  → guarda o NOVO restore_token     ← é de uso único e roda a cada sessão
  → ConnectToEIS → libei
```

* Pede **só teclado**. Nada de captura de tela, ponteiro ou toque.
* O `restore_token` fica no KWallet, junto das chaves — é uma capacidade de
  injetar teclas, não um dado comum.
* Na partida a sessão é restaurada **em silêncio** se houver token. Sem token,
  o `surr` **não** abre diálogo sozinho: o diálogo rouba foco, e fazer isso no
  meio de um ditado mandaria o `Ctrl+V` para a janela errada.
* Se a permissão for negada ou o EIS ficar indisponível, o texto **continua indo
  para a área de transferência** e a cápsula avisa. Nunca se perde uma
  transcrição por causa de uma colagem que falhou.
* Para reparar: **bandeja → Autorizar colagem automática…**

---

## Como alterar o atalho

Quem decide o gatilho é o compositor — `preferred_trigger` é só uma sugestão.

* **Configurações → Alterar atalho…** (chama `ConfigureShortcuts` do portal), ou
* **Configurações do Sistema → Atalhos → Surr**.

A interface sempre mostra o gatilho realmente em vigor
(`trigger_description`), nunca "Pause" cravado no código.

---

## Modo de depuração

```bash
QT_LOGGING_RULES="surr.*.debug=true" surr
```

Categorias: `surr.app`, `surr.audio`, `surr.net`, `surr.portal`,
`surr.shortcut`, `surr.inject`, `surr.ui`.

**Nunca são registrados**: o texto transcrito, amostras de áudio, chaves de API
ou restore tokens. Só tamanhos, durações e códigos de erro.

O áudio é apagado logo depois do envio. Para mantê-lo, ligue
**Configurações → Modo de depuração**.

### Ferramentas de diagnóstico

```bash
./build/bin/surr-probe             # o Deactivated do portal chega ao soltar?
./build/bin/surr-probe-audio       # grava 3 s e valida o WAV
./build/bin/surr-probe-paste       # cadeia completa: sessão → clipboard → Ctrl+V
./build/bin/surr-probe-transcribe <arquivo.wav> [groq|cepify]
```

Testemunha independente do atalho (o compositor não deixa simular a tecla):

```bash
dbus-monitor --session "type='signal',interface='org.freedesktop.portal.GlobalShortcuts'"
```

---

## Arquitetura

```
main.cpp ─┬─ daemon      → LayerShellQt + bandeja + cápsula
          └─ --settings  → processo separado, janela normal

ApplicationController
 ├─ GlobalShortcutManager  (portal GlobalShortcuts v2)
 ├─ DictationStateMachine  (Idle→Recording→Finalizing→Transcribing→Delivering)
 ├─ AudioRecorder ──────── AudioEngine (thread própria) ── PcmConverter ── WavWriter
 ├─ ITranscriptionProvider ─┬─ GroqProvider
 │                          └─ CepifyProvider      (ambos via OpenAiCompatibleProvider)
 ├─ DeliveryCoordinator ──┬─ ClipboardManager (portal Clipboard)
 │                        └─ EisInjector      (libei)
 ├─ OverlayController      (Qt Quick + LayerShellQt)
 ├─ TrayIcon               (KStatusNotifierItem)
 ├─ SecretStore            (KWallet)
 └─ Settings               (KConfig)
```

### Por que dois processos

`LayerShellQt::Shell::useLayerShell()` vale para o **processo inteiro**: toda
janela vira superfície de layer-shell, com padrão `LayerTop` ancorado nas quatro
bordas. Numa janela de configurações isso significaria sem decoração, sem barra
de tarefas e esticada na tela toda. Então o daemon tem só a cápsula, e
`--settings` roda em processo separado. Os dois conversam por D-Bus
(`br.com.cepify.Surr.Control`), e configuração e segredos já são compartilhados
via KConfig e KWallet.

### Camadas do build

| Alvo | Depende de | Testável sem sessão gráfica |
|---|---|---|
| `surr_core` | só `Qt6::Core` + `Qt6::Network` | **sim, 100%** |
| `surr_platform` | + DBus, Multimedia, libei, KF6 | precisa do barramento e de um plugin QPA |
| `surr_ui` | + Quick, LayerShellQt, SNI | precisa de display |

A separação é enforcement, não decoração: como `surr_core` não linka
`Qt6::DBus`, um `#include <QDBusMessage>` perdido na máquina de estados vira
**erro de compilação**, e os testes dessa camada continuam rodando sem
sessão gráfica nenhuma.

Um teste que linke `surr_platform`, porém, **não** é headless: essa biblioteca
linka `Qt6::Gui` como PUBLIC, o CMake propaga `-DQT_GUI_LIB` para o objeto do
teste e o `QTEST_MAIN` passa a instanciar um `QGuiApplication`, que exige um
plugin QPA. É o caso do `settings`, o único teste fora do `surr_core`.

---

## Testes

```bash
ctest --test-dir build --output-on-failure
```

Cobrem a máquina de estados (todas as arestas de corrida de apertar/soltar),
o header do WAV, a conversão de PCM contra os formatos reais dos microfones
desta máquina, o parsing das respostas dos dois providers, o mapeamento
HTTP→frase acionável e a resolução de proxy.

### Sem sessão gráfica (CI, contêiner, SSH)

Seis dos sete testes linkam só o `surr_core` e rodam em qualquer lugar. O
`settings` linka `surr_platform` e precisa de um plugin QPA — sem isso ele
aborta antes do primeiro caso, com *"no Qt platform plugin could be
initialized"*. O `offscreen` resolve:

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Numa sessão Plasma a falha não aparece nem apagando `DISPLAY` e
`WAYLAND_DISPLAY`: o plugin wayland acha o socket sozinho pelo
`XDG_RUNTIME_DIR`. Para reproduzir o ambiente sem sessão gráfica de verdade é
preciso apontar o `XDG_RUNTIME_DIR` para um diretório vazio.

### `appstreamtest` não é deste projeto

O oitavo teste vem de graça do `KDECMakeSettings` do ECM, e não valida
`data/br.com.cepify.Surr.metainfo.xml`: ele lê o `build/install_manifest.txt` e
valida o metainfo **já instalado**. Antes do primeiro `cmake --install` ele
imprime *"Not installed yet, skipping"* e passa sem olhar nada; depois, valida
a cópia instalada, não a do repositório. Ao mexer no metainfo:

```bash
appstreamcli validate --no-net data/br.com.cepify.Surr.metainfo.xml
cmake --install build && ctest --test-dir build -R appstreamtest
```

Uma unit test não substitui a validação dos portais: veja
`docs/ACHADOS-DO-AMBIENTE.md` para o que foi medido no ambiente real.

---

## Rede com proxy

Numa rede que exige proxy há uma armadilha específica: as variáveis
`http_proxy` normalmente vêm do `.zshrc`, e **o autostart do KDE não roda o
`.zshrc`**. Do terminal funciona, da sessão não.

O `surr` resolve o proxy na ordem: variáveis de ambiente → `~/.config/kioslaverc`
(configuração do KDE), tratando o formato do KDE que grava o endereço com
**espaço no lugar dos dois-pontos** (`http://127.0.0.1 3128`).

Teste no cenário do autostart:

```bash
env -u http_proxy -u https_proxy -u all_proxy surr
```

---

## Licença

GPL-3.0-or-later.

### Conferir a cápsula sem ditar

```bash
surr --preview
```

Percorre os estados visuais (preparando → gravando → transcrevendo → pronto)
por 8 segundos, sem gravar áudio nem gastar chamada de API. Útil para conferir
posicionamento em vários monitores e aparência depois de mexer no QML.

A cápsula aparece no monitor que o KWin reporta como ativo
(`org.kde.KWin.activeOutputName`). `QCursor::pos()` **não serve** no Wayland:
o compositor não expõe a posição do ponteiro e o Qt devolve (0,0) — veja
`docs/ACHADOS-DO-AMBIENTE.md`.
