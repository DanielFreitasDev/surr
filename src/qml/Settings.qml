import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/*
 * Configurações. Roda no processo `--settings`, que não liga o layer-shell,
 * então esta é uma janela normal, com decoração e barra de tarefas.
 */
Rectangle {
    id: root
    color: palette.window

    property int pad: 18

    ScrollView {
        anchors.fill: parent
        anchors.margins: 0
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width
            spacing: 0

            // ---- Transcrição -------------------------------------------
            Section {
                title: "Transcrição"
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Serviço"; Layout.preferredWidth: 150 }
                    ComboBox {
                        id: providerBox
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "id"
                        model: [
                            { id: "groq", label: "Groq (nuvem)" },
                            { id: "cepify", label: "Whisper próprio" }
                        ]
                        Component.onCompleted: currentIndex = indexOfValue(backend.provider)
                        onActivated: backend.provider = currentValue
                        // Roda do mouse trocando o valor sem querer é um
                        // clássico de formulário; desligada de propósito.
                        WheelHandler { onWheel: (e) => e.accepted = true }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: backend.provider === "groq"
                    Label { text: "Modelo"; Layout.preferredWidth: 150 }
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "id"
                        model: [
                            { id: "whisper-large-v3-turbo", label: "whisper-large-v3-turbo (rápido)" },
                            { id: "whisper-large-v3", label: "whisper-large-v3 (mais preciso)" }
                        ]
                        Component.onCompleted: currentIndex = indexOfValue(backend.groqModel)
                        onActivated: backend.groqModel = currentValue
                        WheelHandler { onWheel: (e) => e.accepted = true }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Idioma"; Layout.preferredWidth: 150 }
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "label"
                        valueRole: "id"
                        // Vazio = detecção automática: o parâmetro é omitido.
                        // A API espera ISO-639-1, então "pt", nunca "pt-BR".
                        model: [
                            { id: "", label: "Automático" },
                            { id: "pt", label: "Português" },
                            { id: "en", label: "Inglês" },
                            { id: "es", label: "Espanhol" }
                        ]
                        Component.onCompleted: currentIndex = indexOfValue(backend.language)
                        onActivated: backend.language = currentValue
                        WheelHandler { onWheel: (e) => e.accepted = true }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: backend.provider === "cepify"
                    Label { text: "Endereço"; Layout.preferredWidth: 150 }
                    TextField {
                        Layout.fillWidth: true
                        text: backend.cepifyBaseUrl
                        placeholderText: "https://whisper.exemplo.com.br"
                        onEditingFinished: backend.cepifyBaseUrl = text
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: backend.provider === "cepify"
                    Label { text: "Vocabulário"; Layout.preferredWidth: 150 }
                    TextField {
                        Layout.fillWidth: true
                        text: backend.hotwords
                        placeholderText: "termos do seu domínio, separados por vírgula"
                        onEditingFinished: backend.hotwords = text
                    }
                }
            }

            // ---- Credenciais -------------------------------------------
            Section {
                title: "Credenciais"
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    opacity: 0.75
                    font.pixelSize: 12
                    text: backend.walletAvailable
                          ? "As chaves ficam no KWallet, nunca em arquivo de configuração. A variável de ambiente GROQ_API_KEY, se definida, tem prioridade sobre a carteira."
                          : "O KWallet não está disponível. As chaves ficam num arquivo local com permissão restrita (0600)."
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: backend.provider === "groq"
                    Label { text: "Chave Groq"; Layout.preferredWidth: 150 }
                    TextField {
                        id: groqKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: backend.groqKeyFromEnvironment
                                         ? "definida pela variável de ambiente"
                                         : (backend.hasGroqKey ? "•••••••• (configurada)" : "gsk_…")
                        enabled: !backend.groqKeyFromEnvironment
                    }
                    Button {
                        text: "Salvar"
                        enabled: groqKeyField.text.length > 0
                        onClicked: { backend.setGroqKey(groqKeyField.text); groqKeyField.clear() }
                    }
                    Button {
                        text: "Apagar"
                        enabled: backend.hasGroqKey && !backend.groqKeyFromEnvironment
                        onClicked: backend.clearGroqKey()
                    }
                }

                Button {
                    visible: backend.provider === "groq" && backend.legacyKeyAvailable()
                             && !backend.hasGroqKey
                    text: "Importar a chave do Sussurro"
                    onClicked: backend.importLegacyKey()
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: backend.provider === "cepify"
                    Label { text: "Chave"; Layout.preferredWidth: 150 }
                    TextField {
                        id: cepifyKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: backend.hasCepifyKey ? "•••••••• (configurada)" : "chave da API"
                    }
                    Button {
                        text: "Salvar"
                        enabled: cepifyKeyField.text.length > 0
                        onClicked: { backend.setCepifyKey(cepifyKeyField.text); cepifyKeyField.clear() }
                    }
                }

                Button {
                    text: "Testar conexão"
                    onClicked: backend.testProvider()
                }
            }

            // ---- Áudio --------------------------------------------------
            Section {
                title: "Áudio"
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Microfone"; Layout.preferredWidth: 150 }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["Padrão do sistema"].concat(backend.inputDevices)
                        Component.onCompleted: {
                            var i = backend.inputDeviceIds.indexOf(backend.inputDeviceId)
                            currentIndex = i < 0 ? 0 : i + 1
                        }
                        onActivated: backend.inputDeviceId =
                            currentIndex === 0 ? "" : backend.inputDeviceIds[currentIndex - 1]
                        WheelHandler { onWheel: (e) => e.accepted = true }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Duração mínima"; Layout.preferredWidth: 150 }
                    SpinBox {
                        from: 0; to: 2000; stepSize: 50
                        value: backend.minimumHoldMs
                        onValueModified: backend.minimumHoldMs = value
                        textFromValue: (v) => v + " ms"
                        WheelHandler { onWheel: (e) => e.accepted = true }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        font.pixelSize: 11
                        text: "Toques mais curtos que isso são descartados sem chamar a API."
                    }
                }
            }

            // ---- Atalho e colagem ---------------------------------------
            Section {
                title: "Atalho e colagem"
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "O atalho é gerenciado pelo sistema. Sugestão inicial: Pause/Break."
                    }
                    Button {
                        text: "Alterar atalho…"
                        onClicked: backend.configureShortcut()
                    }
                }

                CheckBox {
                    text: "Colar automaticamente onde o cursor estiver"
                    checked: backend.autoPaste
                    onToggled: backend.autoPaste = checked
                }

                CheckBox {
                    text: "Usar Ctrl+Shift+V (colagem em terminais)"
                    checked: backend.terminalPaste
                    onToggled: backend.terminalPaste = checked
                    enabled: backend.autoPaste
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.75
                        font.pixelSize: 12
                        text: "A colagem automática usa o portal do sistema. A autorização é pedida uma vez e fica guardada."
                    }
                    Button {
                        text: "Autorizar…"
                        onClicked: backend.authorizePaste()
                    }
                }
            }

            // ---- Aplicativo ----------------------------------------------
            Section {
                title: "Aplicativo"
                Layout.fillWidth: true

                CheckBox {
                    text: "Iniciar com a sessão"
                    checked: backend.autostart
                    onToggled: backend.autostart = checked
                }
                CheckBox {
                    text: "Modo de depuração: manter os arquivos de áudio"
                    checked: backend.debugKeepAudio
                    onToggled: backend.debugKeepAudio = checked
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    opacity: 0.7
                    font.pixelSize: 11
                    text: "Por padrão o áudio é apagado logo após a transcrição, e nem o áudio nem o texto vão para os logs."
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }

    // Barra de status: uma linha, sempre visível, sem diálogo modal.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: statusLabel.implicitHeight + 20
        visible: backend.statusMessage !== ""
        color: Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.14)

        Label {
            id: statusLabel
            anchors.fill: parent
            anchors.margins: 10
            wrapMode: Text.Wrap
            text: backend.statusMessage
        }
    }

    // Cabeçalho + conteúdo recuado. O conteúdo vai para o layout interno
    // via default property, para herdar as margens sem repeti-las em cada item.
    component Section: ColumnLayout {
        id: section
        property string title: ""
        default property alias content: inner.data
        spacing: 8

        Item { Layout.preferredHeight: 10 }
        Label {
            text: section.title
            font.bold: true
            font.pixelSize: 14
            Layout.leftMargin: root.pad
            Layout.rightMargin: root.pad
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: root.pad
            Layout.rightMargin: root.pad
            Layout.preferredHeight: 1
            color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, 0.12)
        }
        ColumnLayout {
            id: inner
            Layout.fillWidth: true
            Layout.leftMargin: root.pad
            Layout.rightMargin: root.pad
            spacing: 10
        }
    }
}
