import QtQuick
import QtQuick.Effects

/*
 * A cápsula. Discreta, sem foco, coerente com o Plasma.
 *
 * Os estados vêm do C++ como inteiro (OverlayController::State) porque a
 * janela é criada em C++, sem contexto de propriedades — manter a interface
 * mínima entre as duas camadas.
 */
Item {
    id: root

    // 0 Hidden · 1 Preparing · 2 Recording · 3 Transcribing · 4 Success · 5 Error
    property int state: 0
    property real level: 0.0
    property real seconds: 0.0
    property string message: ""

    readonly property bool isRecording: state === 1 || state === 2
    readonly property color accent: {
        if (state === 5) return "#e05252"
        if (state === 4) return "#4caf72"
        if (state === 3) return "#5a9fd4"
        return "#e05252"
    }

    Rectangle {
        id: capsule
        anchors.centerIn: parent
        width: parent.width - 16
        height: parent.height - 16
        radius: height / 2
        color: Qt.rgba(0.13, 0.13, 0.15, 0.92)
        border.color: Qt.rgba(1, 1, 1, 0.10)
        border.width: 1

        opacity: root.state === 0 ? 0 : 1
        scale: root.state === 0 ? 0.92 : 1
        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }

        Row {
            anchors.centerIn: parent
            spacing: 12

            // Ponto de gravação: pulsa enquanto grava, fica parado no resto.
            Rectangle {
                id: dot
                anchors.verticalCenter: parent.verticalCenter
                width: 11; height: 11; radius: 5.5
                color: root.accent

                SequentialAnimation on opacity {
                    running: root.isRecording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 620; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0;  duration: 620; easing.type: Easing.InOutSine }
                }
                // Ao sair da gravação a animação para onde estiver; devolve
                // o ponto à opacidade cheia para os estados seguintes.
                Connections {
                    target: root
                    function onIsRecordingChanged() {
                        if (!root.isRecording) dot.opacity = 1
                    }
                }

                // "Preparando" é visualmente distinto de "gravando": o
                // dispositivo leva ~140 ms até a primeira amostra, e dizer
                // "gravando" antes disso faria o usuário falar no vazio.
                Rectangle {
                    anchors.centerIn: parent
                    width: 20; height: 20; radius: 10
                    color: "transparent"
                    border.color: root.accent
                    border.width: 1.5
                    visible: root.state === 1
                    opacity: 0.5
                    RotationAnimation on rotation {
                        running: root.state === 1
                        loops: Animation.Infinite
                        from: 0; to: 360; duration: 1100
                    }
                }
            }

            // Barras de nível, alimentadas pelo áudio real.
            Row {
                id: bars
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                visible: root.isRecording

                Repeater {
                    model: 18
                    Rectangle {
                        id: bar
                        width: 3
                        radius: 1.5
                        color: root.accent
                        opacity: 0.85
                        anchors.verticalCenter: parent.verticalCenter

                        // Cada barra tem seu próprio alvo, deslocado no tempo,
                        // para o conjunto parecer uma onda e não um bloco.
                        property real target: 4
                        height: target

                        Behavior on height {
                            NumberAnimation { duration: 90; easing.type: Easing.OutQuad }
                        }

                        Timer {
                            interval: 55 + index * 4
                            running: root.isRecording
                            repeat: true
                            onTriggered: {
                                var centre = 1.0 - Math.abs(index - 8.5) / 11.0
                                var noise = 0.65 + Math.random() * 0.35
                                bar.target = 4 + root.level * 26 * centre * noise
                            }
                        }
                        onVisibleChanged: if (!visible) target = 4
                    }
                }
            }

            // Pontinhos de "transcrevendo".
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                visible: root.state === 3
                Repeater {
                    model: 3
                    Rectangle {
                        width: 6; height: 6; radius: 3
                        color: root.accent
                        anchors.verticalCenter: parent.verticalCenter
                        SequentialAnimation on opacity {
                            running: root.state === 3
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 150 }
                            NumberAnimation { to: 0.25; duration: 380 }
                            NumberAnimation { to: 1.0; duration: 380 }
                            PauseAnimation { duration: (2 - index) * 150 }
                        }
                    }
                }
            }

            // Marca de concluído.
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.state === 4
                text: "✓"
                color: root.accent
                font.pixelSize: 20
                font.bold: true
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (root.state === 1) return "Preparando…"
                    if (root.state === 2) return root.seconds.toFixed(1) + " s"
                    if (root.state === 3) return "Transcrevendo…"
                    if (root.state === 4) return root.message !== "" ? root.message : "Pronto"
                    if (root.state === 5) return root.message
                    return ""
                }
                color: root.state === 5 ? "#ffb4b4" : "#f2f2f4"
                font.pixelSize: 13
                font.family: "Noto Sans"
                // Mensagem de erro pode ser longa; elide em vez de esticar a
                // cápsula pela tela toda.
                elide: Text.ElideRight
                width: Math.min(implicitWidth, capsule.width - 120)
            }
        }
    }

    MultiEffect {
        source: capsule
        anchors.fill: capsule
        shadowEnabled: true
        shadowBlur: 0.6
        shadowColor: Qt.rgba(0, 0, 0, 0.45)
        shadowVerticalOffset: 3
        opacity: capsule.opacity
    }
}
