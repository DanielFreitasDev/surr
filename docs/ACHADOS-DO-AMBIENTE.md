# Achados medidos neste ambiente

Kubuntu 26.04.1 · Plasma 6.6.6 · KWin 6.6.6 · Wayland · Qt 6.10.2 · KF6 6.24.0

Medições feitas com `surr-probe` e introspecção D-Bus, não presumidas.

## 1. Atalho global: o portal GlobalShortcuts é viável (decisão tomada)

O app anterior (`sussurro`) usou KGlobalAccel por D-Bus e deixou registrado que
faltava medir uma coisa antes de migrar para o portal: *se o `Deactivated`
chega de forma confiável ao soltar a tecla*. Medido:

```
[BindShortcuts] ok, 1 atalho(s):
    id=dictate  descrição="Segurar para ditar"  gatilho="Pause"

  ACTIVATED   id=dictate  ts=0  (nº 1)
  DEACTIVATED id=dictate  ts=0
       Activated recebidos enquanto segurava: 1
       Tempo segurado: 1077 ms (local) / -1 ms (timestamps do portal)
```

Conclusões:

- **`Deactivated` chega ao soltar.** Push-to-talk pelo portal funciona.
  → O portal é o caminho principal, como pede o requisito 6.
- **`preferred_trigger: "Pause"` foi aceito** e voltou como
  `trigger_description = "Pause"`. Formato da spec Freedesktop Shortcuts:
  nome de keysym do xkbcommon sem o prefixo `XKB_KEY_`, sensível a maiúsculas.
- **O auto-repeat não vaza para o portal**: 1,08 s segurados produziram
  exatamente **1** `Activated`. O KWin encaminha repetição por
  `globalShortcutRepeated`, que o portal não repassa.
- **`BindShortcuts` não abriu diálogo** nesta máquina.

## 2. Os timestamps do portal são ZERO — não dá para medir tempo com eles

`Activated` e `Deactivated` chegaram ambos com `ts=0` nesta build do KDE.
A spec diz microssegundos monotônicos, mas aqui o valor é inútil.

**Consequência obrigatória:** a duração da tecla segurada — que é o critério
de "curto demais" — tem de ser medida com um `QElapsedTimer` local. Confiar
no timestamp do portal faria todo ditado ser lido como duração zero e
descartado. O código cruza os dois e prefere o local quando o do portal é
implausível.

## 3. `org.freedesktop.host.portal.Registry` fica no objeto principal

Não existe `/org/freedesktop/host/portal/Registry`. A interface está em
`/org/freedesktop/portal/desktop`:

```
Object does not exist at path "/org/freedesktop/host/portal/Registry"
```

`Register(app_id, options)` precisa ser a **primeira** chamada ao portal e só
pode ser feita uma vez. Sem ela o app_id vem do systemd scope de quem lançou
o processo — foi assim que a permissão do app anterior foi parar em
`org.kde.konsole`.

O app_id precisa ter pelo menos três elementos separados por ponto; `surr`
sozinho seria recusado. Daí `br.com.cepify.Surr`.

## 4. A tecla Pause estava ocupada pelo app anterior

```
isGlobalShortcutAvailable(Key_Pause) → (false,)
/component/sussurro  isActive()      → (true,)
```

Resolvido encerrando o processo e movendo
`~/.config/autostart/sussurro.desktop` para
`~/.config/autostart-disabled-by-surr/`. Reversível.

## 5. Esta máquina exige proxy — e o autostart não o herda

```
kioslaverc: ProxyType=1
            httpProxy=http://127.0.0.1 3128     ← espaço, não dois-pontos
env:        http_proxy=http://127.0.0.1:3128    ← vem do .zshrc
```

O autostart do KDE **não** roda o `.zshrc`. Do terminal a rede funciona, da
sessão não. É preciso resolver o proxy pela configuração do KDE, não só pelo
ambiente. Teste obrigatório:

```bash
env -u http_proxy -u https_proxy -u all_proxy ./surr
```

## 6. Nenhum microfone oferece 16 kHz mono nativamente

| Dispositivo | Formato nativo |
|---|---|
| Samsung USB-C (padrão) | s16le 2ch 48000 Hz |
| Logitech C925e | s16le 2ch 32000 Hz |
| Intel analógico | s32le 2ch 48000 Hz |

Todos são múltiplos inteiros de 16 kHz (48/16 = 3, 32/16 = 2), então a
conversão é decimação por inteiro depois de um passa-baixa — não é preciso
reamostrador de razão arbitrária para o hardware presente.

## 7. Armadilha de shell ao testar

`pkill -f "padrão"` casa o **próprio shell invocador** e o mata (saída 144).
Aconteceu aqui ao tentar encerrar a sonda. Usar `pgrep -x` e pular `$$`/`$PPID`.
O `LEARNING.md` do sussurro já documentava exatamente isso.

## 8. `Registry.Register` exige um `.desktop` instalado E com `Exec` resolvível

```
[Registry] Could not register app ID: App info not found for 'br.com.cepify.Surr'
```

Duas condições, ambas necessárias:

1. Existir `~/.local/share/applications/<app_id>.desktop` (ou em `XDG_DATA_DIRS`).
2. O binário do `Exec` **existir no `PATH`**.

A segunda foi isolada por bisseção: um `.desktop` idêntico com `Exec=surr`
(binário ainda não instalado) é invisível para o GIO; trocando por um
`Exec` que resolve, o mesmo arquivo passa a ser encontrado na hora.

```
Exec=surr           → Gio.DesktopAppInfo.new(...) devolve NULL
Exec=/usr/bin/true  → devolve "Surr"
```

Maiúsculas no nome não são problema (`org.gnome.Meld.desktop` é encontrado).

**Consequência prática:** o `Registry.Register` — e portanto a persistência
correta da permissão de colagem sob o app_id certo — só funciona depois que o
`surr` está instalado. Rodando direto da pasta de build sem instalar, o
app_id cai no scope do systemd de quem lançou o processo (foi assim que a
permissão do app anterior foi parar em `org.kde.konsole`).

## 9. A cadeia de entrega funciona ponta a ponta (validada)

`surr-probe-paste` executou a sequência completa e o texto apareceu sozinho
no cursor do Kate:

```
[Registry] app_id = br.com.cepify.Surr
[Token] restore_token existente encontrado
[Token] novo restore_token gravado (uso único — sempre substituir)
sessão RemoteDesktop iniciada; teclado= true clipboard= true
EIS conectado
teclado virtual PRONTO
[Clipboard] seleção publicada pelo portal.
[EIS] Ctrl+V enviado.
```

Conteúdo do arquivo do Kate depois, salvo por D-Bus para conferência objetiva:

```
Clique nesta linha e deixe o cursor aqui:

Esse texto foi escrito usando o Surr
Acentuação: ação, coração, português, ãõ, ç, áéíóú
Símbolos e emoji: — ½ € 🎤 ✅
```

E a área de transferência, conferida por fora com `wl-paste`, continuava com
o mesmo texto e oferecia os cinco tipos MIME declarados.

### O que isso retira de risco

1. **Área de transferência sem foco.** Era o risco maior: um cliente Wayland
   nativo precisa de um serial de input de superfície com foco para chamar
   `wl_data_device.set_selection`. A cápsula do surr é layer-shell sem foco,
   então `QClipboard::setText()` seria descartado **em silêncio** e o Ctrl+V
   colaria o conteúdo anterior. O `org.freedesktop.portal.Clipboard`, atado à
   sessão RemoteDesktop, não tem essa exigência — e funciona.
2. **Unicode.** Acentos, cedilha, til, travessão, ½, € e emoji sobreviveram
   intactos. É o argumento a favor de colar em vez de digitar tecla a tecla.
3. **Persistência.** O `Start` não abriu diálogo: o `restore_token` guardado
   foi aceito e rotacionado. O usuário autoriza uma vez.
4. **Injeção EIS.** `ConnectToEIS` + libei entregaram Ctrl+V ao aplicativo em
   foco, sem `xdotool`, sem XTEST, sem `/dev/uinput`, sem `ydotool`.

## 10. Traço de um ciclo completo (instrumentado)

Com `QT_LOGGING_RULES="surr.*.debug=true"`, um ditado real produz:

```
Activated recebido: id="dictate" ... keyDown= false
captura iniciada, ciclo 1
Deactivated recebido: id="dictate" ... keyDown= true
captura encerrada, 2.02494 s
transcrição enviada "groq" "whisper-large-v3-turbo" 64842 bytes
transcrição concluída "groq" 840 ms 25 caracteres
seleção publicada, 25 caracteres
transferência de seleção "text/plain;charset=utf-8" ok
```

Exatamente **um** `Activated` e **um** `Deactivated` por ditado. A última
linha é o portal avisando que alguém leu a seleção — ou seja, a colagem
consumiu o texto de verdade.

Repare no que o log **não** tem: nem o texto transcrito, nem amostras de
áudio, nem chave, nem token. Só contagens, durações e códigos.

## 11. `pgrep -f` derruba o próprio shell — de novo

Aconteceu duas vezes nesta sessão (saída 144), a segunda já sabendo do risco.
A forma segura:

```bash
for pid in $(pgrep -x surr); do
  [ "$pid" = "$$" ] && continue
  [ "$pid" = "$PPID" ] && continue
  kill -TERM "$pid"
done
```

## 12. Duas instâncias na lista de processos são o comportamento correto

`pgrep -xc surr` devolvendo 2 costuma ser o daemon **mais** a janela de
configurações (`surr --settings`), que roda em processo separado de propósito.
Conferir a linha de comando antes de concluir que a instância única falhou.

## 13. `QCursor::pos()` devolve (0,0) no Wayland

Medido, com três monitores lado a lado (DP-1 em x=0, DP-3 em x=1920,
DP-2 em x=3840):

```
surr.ui: cursor em QPoint(0,0) -> tela "DP-1" QRect(0,0 1920x1080)
```

O compositor **não expõe a posição global do ponteiro** aos clientes — é uma
fronteira de privacidade do Wayland, não um bug do Qt. Qualquer heurística de
"a tela onde está o cursor" baseada em `QCursor::pos()` sempre aponta para a
primeira tela, e a cápsula fica presa nela.

### O que funciona no Plasma

```
gdbus call --session --dest org.kde.KWin --object-path /KWin \
  --method org.kde.KWin.activeOutputName
→ ('DP-2',)
```

Os nomes casam com `QScreen::name()`. A chamada é feita de forma **assíncrona**
— uma chamada bloqueante rodaria um event loop aninhado e poderia reordenar os
sinais de press/release do portal. A resposta é local e chega em poucos
milissegundos, muito antes dos ~140 ms que o microfone leva para entregar a
primeira amostra, então a cápsula não atrasa de forma perceptível.

### E a saída no layer-shell é escolhida na criação da superfície

`setScreen()` numa janela já criada não move nada: a saída entra no
`get_layer_surface()`. Para trocar de monitor é preciso destruir a superfície
(`QWindow::destroy()`) e deixar o `show()` recriá-la na saída certa.

Cuidado com `setWantsToBeOnActiveScreen(true)`: nesse modo o LayerShellQt passa
uma saída **nula** e deixa o compositor decidir, ignorando o `setScreen()`
explícito. Para escolher a tela por conta própria, tem de ser `false`.
