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
_(vazio — a primeira entrada vai aqui)_

### Pegadinhas de ambiente, dependências ou bibliotecas
- [2026-08-31 — O teste `settings` não roda sem display; os outros rodam](#2026-08-31--o-teste-settings-não-roda-sem-display-os-outros-rodam)
- [2026-08-31 — `appstreamtest` valida o metainfo INSTALADO, não o do repositório](#2026-08-31--appstreamtest-valida-o-metainfo-instalado-não-o-do-repositório)
- [2026-08-31 — O separador de dígitos do C++ quebra varredores de código](#2026-08-31--o-separador-de-dígitos-do-c-quebra-varredores-de-código)
- [2026-08-31 — `PIPESTATUS` é do bash; no zsh o veredito do `verificar.sh` some](#2026-08-31--pipestatus-é-do-bash-no-zsh-o-veredito-do-verificarsh-some)

### Decisões de arquitetura
- [2026-08-31 — O estado da cápsula atravessa a fronteira QML↔C++ como inteiro cru](#2026-08-31--o-estado-da-cápsula-atravessa-a-fronteira-qmlc-como-inteiro-cru)
- [2026-08-31 — O veredito de "pronto" é o código de saída do `verificar.sh`](#2026-08-31--o-veredito-de-pronto-é-o-código-de-saída-do-verificarsh)
- [2026-08-31 — O projeto está CONGELADO: auditoria nova só com defeito real de uso](#2026-08-31--o-projeto-está-congelado-auditoria-nova-só-com-defeito-real-de-uso)

### Abordagens que NÃO funcionaram
_(vazio)_

### Notas de performance
_(vazio)_

---

## Bugs e soluções

_Nenhuma entrada ainda._

---

## Pegadinhas de ambiente, dependências ou bibliotecas

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

## Decisões de arquitetura

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
CONGELADO em 93dc02a — 2026-08-31. Só mexer com pedido explícito meu ou defeito
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
