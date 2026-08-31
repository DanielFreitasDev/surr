#!/usr/bin/env bash
#
# verificar.sh — decide sozinho se o surr cumpre o contrato do ACEITACAO.md.
#
# Sai 0 apenas se TODOS os checks passarem. Um check vermelho não aborta os
# outros: a saída sempre mostra a lista inteira.
#
# Não altera o projeto, nem a instalação em ~/.local, nem ~/.config/surrrc.
# A compilação e a instalação de teste acontecem num diretório temporário que
# é apagado no fim.
#
# Não usa rede: nenhuma chave de API é lida, nenhuma chamada é feita. Isso é
# deliberado — ver "Limitações aceitas" no ACEITACAO.md.

set -u -o pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$RAIZ" || exit 2

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

BUILD="$TMP/build"
PREFIXO="$TMP/prefixo"

TOTAL=0
PASSOU=0
FALHOU_LISTA=()

# ---------------------------------------------------------------------------
# Motor: cada check é uma função que sai 0 (verde) ou != 0 (vermelho).
# A saída da função só é impressa quando ela falha — verde não polui.
# ---------------------------------------------------------------------------
check() {
    local nome="$1" descricao="$2" fn="$3"
    TOTAL=$((TOTAL + 1))
    local saida rc
    saida="$("$fn" 2>&1)"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        PASSOU=$((PASSOU + 1))
        printf '  \033[32m✔\033[0m %-18s %s\n' "$nome" "$descricao"
    else
        FALHOU_LISTA+=("$nome")
        printf '  \033[31m✘\033[0m %-18s %s\n' "$nome" "$descricao"
        printf '%s\n' "$saida" | sed 's/^/        │ /'
    fi
}

# Checks que dependem de artefatos do build ficam vermelhos, não pulados.
#
# O marcador é arquivo, não variável: o check() roda cada função dentro de
# $(...), ou seja, num subshell — uma variável setada lá dentro não voltaria.
exige_build() {
    if [ ! -f "$TMP/build-ok" ]; then
        echo "depende do check 'build', que falhou"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# 1. build — compila do zero, em Release, com Ninja.
# ---------------------------------------------------------------------------
c_build() {
    cmake -S "$RAIZ" -B "$BUILD" -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX="$PREFIXO" >"$TMP/cmake.log" 2>&1 || {
        tail -30 "$TMP/cmake.log"; return 1; }
    cmake --build "$BUILD" >"$TMP/build.log" 2>&1 || {
        tail -40 "$TMP/build.log"; return 1; }
    touch "$TMP/build-ok"
}

# ---------------------------------------------------------------------------
# 2. instala — o install produz os cinco artefatos que o portal e o Plasma
#    precisam encontrar. Sem o .desktop instalado, o Registry.Register falha e
#    a permissão de colagem vai parar sob o app-id errado.
# ---------------------------------------------------------------------------
c_instala() {
    exige_build || return 1
    cmake --install "$BUILD" >"$TMP/install.log" 2>&1 || {
        tail -20 "$TMP/install.log"; return 1; }
    local faltando=()
    local esperados=(
        "$PREFIXO/bin/surr"
        "$PREFIXO/share/applications/br.com.cepify.Surr.desktop"
        "$PREFIXO/share/metainfo/br.com.cepify.Surr.metainfo.xml"
        "$PREFIXO/share/icons/hicolor/scalable/apps/br.com.cepify.Surr.svg"
        "$PREFIXO/share/knotifications6/surr.notifyrc"
    )
    local f
    for f in "${esperados[@]}"; do
        [ -e "$f" ] || faltando+=("${f#"$PREFIXO"/}")
    done
    if [ ${#faltando[@]} -gt 0 ]; then
        printf 'não instalado: %s\n' "${faltando[@]}"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# 3. testes — a suíte inteira do ctest.
#
#    offscreen porque o teste `settings` linka surr_platform, que puxa
#    Qt6::Gui: sem plugin QPA ele aborta antes do primeiro caso.
#    Roda DEPOIS do install de propósito: só assim o `appstreamtest` do ECM
#    encontra o install_manifest.txt e valida de verdade, em vez de imprimir
#    "Not installed yet, skipping" e passar sem olhar nada.
# ---------------------------------------------------------------------------
c_testes() {
    exige_build || return 1
    QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD" --output-on-failure \
        >"$TMP/ctest.log" 2>&1 || { tail -40 "$TMP/ctest.log"; return 1; }
    grep -q "100% tests passed" "$TMP/ctest.log" || {
        tail -20 "$TMP/ctest.log"; return 1; }
}

# ---------------------------------------------------------------------------
# 4. camadas — surr_core não pode arrastar DBus, GUI, Quick, Multimedia,
#    KConfig ou KWallet. É essa fronteira que mantém a suíte rodando sem
#    sessão gráfica; ela é verificada nos símbolos indefinidos do .a, não no
#    CMakeLists, porque o que vale é o que o linker vai cobrar.
# ---------------------------------------------------------------------------
c_camadas() {
    exige_build || return 1
    local lib="$BUILD/lib/libsurr_core.a"
    [ -f "$lib" ] || { echo "não achei $lib"; return 1; }
    local sujeira
    sujeira="$(nm --undefined-only -C "$lib" 2>/dev/null \
        | grep -oE '\b(QDBus|QGuiApplication|QQuick|QQml|QAudio|QMediaDevices|KConfig|KWallet|LayerShell)[A-Za-z]*' \
        | sort -u)"
    if [ -n "$sujeira" ]; then
        echo "surr_core depende de símbolos proibidos:"
        printf '%s\n' "$sujeira"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# 5. sem-x11 — o ferramental de X11 falha EM SILÊNCIO no Wayland do Plasma 6.
#    Nenhum erro, nenhuma exceção: a colagem só não acontece. É a armadilha
#    que motivou o projeto inteiro, e a que uma sessão futura mais tende a
#    reintroduzir "para consertar a colagem".
# ---------------------------------------------------------------------------
c_sem_x11() {
    local achado
    achado="$(grep -rniE 'xdotool|XTestFake|/dev/uinput|ydotool' src/ tools/ || true)"
    if [ -n "$achado" ]; then
        echo "ferramental de X11 reintroduzido:"
        printf '%s\n' "$achado"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# 6. sem-bloqueio — event loop aninhado na thread principal reordena os sinais
#    Activated/Deactivated do portal, ou seja, quebra o push-to-talk.
#    Exceção única e deliberada: o Registry.Register do main.cpp, que precisa
#    terminar antes de qualquer outra conversa com o portal.
# ---------------------------------------------------------------------------
c_sem_bloqueio() {
    local erros=""
    local loops
    loops="$(grep -rnE 'QEventLoop|waitForFinished' src/ || true)"
    [ -n "$loops" ] && erros+="event loop aninhado / espera bloqueante:"$'\n'"$loops"$'\n'

    local blocks
    blocks="$(grep -rn 'QDBus::Block' src/ || true)"
    local n
    n="$(printf '%s' "$blocks" | grep -c . )"
    if [ "$n" -ne 1 ] || ! printf '%s' "$blocks" | grep -q '^src/main\.cpp:'; then
        erros+="esperava exatamente 1 chamada D-Bus bloqueante, no src/main.cpp; achei $n:"$'\n'"$blocks"$'\n'
    fi

    if [ -n "$erros" ]; then
        printf '%s' "$erros"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# 7. log-limpo — nenhum qCDebug/qCInfo/qCWarning/qCCritical pode transmitir o
#    valor de uma chave, de um restore token ou do texto ditado. Tamanho,
#    duração e código continuam permitidos.
# ---------------------------------------------------------------------------
c_log_limpo() {
    command -v python3 >/dev/null || { echo "python3 não encontrado"; return 1; }
    python3 - src <<'PYEOF'
import pathlib, re, sys

RAIZ = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "src")

PROIBIDO = re.compile(
    r"\b(apikey|api_key|restoretoken|restore_token|token|secret|password|senha"
    r"|text|plaintext|transcript\w*|samples|pcm|payload)\b", re.I)

# .size()/.length()/... revelam contagem, não conteúdo: some com eles antes.
ACESSOR_OK = re.compile(
    r"[A-Za-z_][\w.>\-]*\.(size|length|count|isEmpty|duration|elapsed)\s*\(\s*\)")


def sem_literais_e_comentarios(txt):
    fora, i, n = [], 0, len(txt)
    while i < n:
        c = txt[i]
        if c == '"':
            i += 1
            while i < n and txt[i] != '"':
                i += 2 if txt[i] == '\\' else 1
            i += 1
            fora.append('""')
        elif c == "'" and not (i and (txt[i - 1].isalnum() or txt[i - 1] == "_")):
            # 90'000 é separador de dígito do C++, não literal de caractere.
            i += 1
            while i < n and txt[i] != "'":
                i += 2 if txt[i] == '\\' else 1
            i += 1
            fora.append("''")
        elif txt.startswith("//", i):
            j = txt.find("\n", i)
            i = n if j < 0 else j
        elif txt.startswith("/*", i):
            j = txt.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            fora.append(c)
            i += 1
    return "".join(fora)


falhas = []
for arq in sorted(list(RAIZ.rglob("*.cpp")) + list(RAIZ.rglob("*.h"))):
    limpo = sem_literais_e_comentarios(
        arq.read_text(encoding="utf-8", errors="replace"))
    for m in re.finditer(r"\bqC(?:Debug|Info|Warning|Critical)\s*\(", limpo):
        # pula o argumento de categoria: qCDebug(SURR_AUDIO) não é vazamento.
        i, prof = m.end(), 1
        while i < len(limpo) and prof:
            prof += (limpo[i] == "(") - (limpo[i] == ")")
            i += 1
        fim = limpo.find(";", i)
        corpo = ACESSOR_OK.sub(" ", limpo[i:fim if fim > 0 else len(limpo)])
        for achado in PROIBIDO.findall(corpo):
            linha = limpo.count("\n", 0, m.start()) + 1
            falhas.append("%s:%d: log transmite '%s'" % (arq, linha, achado))

for f in falhas:
    print(f)
sys.exit(1 if falhas else 0)
PYEOF
}

# ---------------------------------------------------------------------------
# 8. qml-enum — a única fronteira do projeto sem rede de proteção do
#    compilador. O estado atravessa como inteiro cru; reordenar o enum não
#    gera erro nenhum, a cápsula só passa a mostrar o estado errado.
# ---------------------------------------------------------------------------
c_qml_enum() {
    command -v python3 >/dev/null || { echo "python3 não encontrado"; return 1; }
    python3 - . <<'PYEOF'
import pathlib, re, sys

raiz = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
h = (raiz / "src/ui/OverlayController.h").read_text(encoding="utf-8")
q = (raiz / "src/qml/Overlay.qml").read_text(encoding="utf-8")

m = re.search(r"enum\s+class\s+State\s*\{([^}]*)\}", h)
if not m:
    print("não achei 'enum class State' em src/ui/OverlayController.h")
    sys.exit(1)
cpp = [v.split("=")[0].strip() for v in m.group(1).split(",") if v.strip()]

linha = re.search(r"//\s*((?:\d+\s+\w+\s*(?:·|\|)?\s*)+)", q)
if not linha:
    print("não achei a tabela de mapeamento (// 0 Hidden · 1 …) em Overlay.qml")
    sys.exit(1)
qml = re.findall(r"(\d+)\s+(\w+)", linha.group(1))

erros = []
if [n for _, n in qml] != cpp:
    erros.append("ordem divergente:\n  C++ = %s\n  QML = %s"
                 % (cpp, [n for _, n in qml]))
if [int(i) for i, _ in qml] != list(range(len(cpp))):
    erros.append("índices do QML não são 0..%d: %s"
                 % (len(cpp) - 1, [i for i, _ in qml]))
usados = {int(x) for x in re.findall(r"state\s*===\s*(\d+)", q)}
faltando = sorted(set(range(len(cpp))) - usados)
if faltando:
    erros.append("Overlay.qml não trata os estados %s (%s)"
                 % (faltando, [cpp[i] for i in faltando]))

for e in erros:
    print(e)
sys.exit(1 if erros else 0)
PYEOF
}

# ---------------------------------------------------------------------------
# 9. desktop-exec — o Exec do .desktop instalado tem de ser caminho absoluto
#    de um binário que existe. O PATH do portal (serviço systemd de usuário)
#    não inclui ~/.local/bin; com Exec relativo o g_desktop_app_info_new()
#    devolve NULL e a permissão de colagem é guardada sob o app-id errado.
# ---------------------------------------------------------------------------
c_desktop_exec() {
    local dsk="$PREFIXO/share/applications/br.com.cepify.Surr.desktop"
    [ -f "$dsk" ] || { echo "não instalado: $dsk (o check 'instala' falhou?)"; return 1; }
    local exec_val
    exec_val="$(grep -m1 '^Exec=' "$dsk" | cut -d= -f2-)"
    case "$exec_val" in
        /*) ;;
        *) echo "Exec não é caminho absoluto: '$exec_val'"; return 1 ;;
    esac
    [ -x "$exec_val" ] || { echo "Exec aponta para algo que não é executável: '$exec_val'"; return 1; }
}

# ---------------------------------------------------------------------------
# 10. desktop-valido — o .desktop gerado passa no validador do padrão XDG.
# ---------------------------------------------------------------------------
c_desktop_valido() {
    command -v desktop-file-validate >/dev/null || {
        echo "desktop-file-validate não encontrado (pacote desktop-file-utils)"; return 1; }
    local dsk="$PREFIXO/share/applications/br.com.cepify.Surr.desktop"
    [ -f "$dsk" ] || { echo "não instalado: $dsk (o check 'instala' falhou?)"; return 1; }
    desktop-file-validate "$dsk"
}

# ---------------------------------------------------------------------------
# 11. metainfo-valido — valida o arquivo DO REPOSITÓRIO, pelo nome.
#     O appstreamtest do ECM (que roda dentro do check 'testes') olha o
#     install_manifest e valida a cópia instalada; se o manifesto não existir
#     ele imprime "Not installed yet, skipping" e passa sem validar nada.
#     Este check não tem como ser pulado.
# ---------------------------------------------------------------------------
c_metainfo_valido() {
    command -v appstreamcli >/dev/null || {
        echo "appstreamcli não encontrado (pacote appstream)"; return 1; }
    appstreamcli validate --no-net data/br.com.cepify.Surr.metainfo.xml
}

# ---------------------------------------------------------------------------
# 12. binario-sobe — prova que o executável recém-compilado inicializa de
#     verdade (linkagem dinâmica, KAboutData, i18n) e ainda oferece as duas
#     opções que o README documenta.
# ---------------------------------------------------------------------------
c_binario_sobe() {
    exige_build || return 1
    local bin="$BUILD/bin/surr"
    [ -x "$bin" ] || { echo "não achei $bin"; return 1; }
    local saida rc
    saida="$(QT_QPA_PLATFORM=offscreen timeout 20 "$bin" --help 2>&1)"
    rc=$?
    [ "$rc" -eq 0 ] || { echo "surr --help saiu $rc:"; printf '%s\n' "$saida"; return 1; }
    printf '%s' "$saida" | grep -q -- '--settings' || { echo "--help não lista --settings"; return 1; }
    printf '%s' "$saida" | grep -q -- '--preview'  || { echo "--help não lista --preview";  return 1; }
}

# ---------------------------------------------------------------------------

echo "verificar.sh — surr"
echo "raiz:  $RAIZ"
echo "build: $TMP (temporário, apagado no fim)"
echo

check build            "compila do zero, em Release, com Ninja"                      c_build
check instala          "instala binário, .desktop, metainfo, ícone e notifyrc"       c_instala
check testes           "ctest: 100% dos testes passam (offscreen)"                   c_testes
check camadas          "surr_core sem DBus, GUI, Quick, Multimedia, KConfig, KWallet" c_camadas
check sem-x11          "nenhum xdotool, XTest, uinput ou ydotool no código"          c_sem_x11
check sem-bloqueio     "nenhum event loop aninhado; 1 chamada bloqueante, no main"   c_sem_bloqueio
check log-limpo        "nenhum log transmite chave, token ou texto ditado"           c_log_limpo
check qml-enum         "OverlayController::State e Overlay.qml alinhados"            c_qml_enum
check desktop-exec     "Exec= é caminho absoluto de um binário existente"            c_desktop_exec
check desktop-valido   "desktop-file-validate aceita o .desktop instalado"           c_desktop_valido
check metainfo-valido  "appstreamcli aceita o metainfo do repositório"               c_metainfo_valido
check binario-sobe     "surr --help sai 0 e lista --settings e --preview"            c_binario_sobe

echo
if [ ${#FALHOU_LISTA[@]} -eq 0 ]; then
    printf '\033[32mPASSOU\033[0m — %d de %d checks\n' "$PASSOU" "$TOTAL"
    exit 0
fi
printf '\033[31mFALHOU\033[0m — %d de %d checks. Vermelhos: %s\n' \
       "$PASSOU" "$TOTAL" "${FALHOU_LISTA[*]}"
exit 1
