#include "EisInjector.h"

#include "SurrDebug.h"

#include <QSocketNotifier>
#include <QTimer>

#include <libei.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

namespace surr {

namespace {
// Códigos evdev de linux/input-event-codes.h — NÃO são keysyms do X11.
constexpr uint32_t KEY_LEFTCTRL = 29;
constexpr uint32_t KEY_LEFTSHIFT = 42;
constexpr uint32_t KEY_V = 47;

/// Espaçamento entre as etapas da combinação. Zero seria válido pelo
/// protocolo (os frames carregam timestamp), mas aplicações reais
/// — Electron, GTK, terminais — filtram modificadores com debounce.
constexpr int kStepMs = 12;
constexpr int kHandshakeTimeoutMs = 3000;
} // namespace

struct EisInjector::Private {
    ei *context = nullptr;
    ei_seat *seat = nullptr;
    ei_device *device = nullptr;
    QSocketNotifier *notifier = nullptr;
    QTimer *stepTimer = nullptr;
    QTimer *handshakeTimer = nullptr;

    bool ready = false;
    uint32_t keyV = KEY_V;

    // Estado da combinação em curso.
    bool chordActive = false;
    bool chordShift = false;
    int step = 0;
    uint32_t sequence = 0;
};

EisInjector::EisInjector(QObject *parent) : QObject(parent), d(new Private)
{
    d->stepTimer = new QTimer(this);
    d->stepTimer->setSingleShot(true);
    d->stepTimer->setInterval(kStepMs);
    connect(d->stepTimer, &QTimer::timeout, this, &EisInjector::onStep);

    d->handshakeTimer = new QTimer(this);
    d->handshakeTimer->setSingleShot(true);
    connect(d->handshakeTimer, &QTimer::timeout, this, [this] {
        if (!d->ready) {
            qCWarning(SURR_INJECT) << "o compositor não liberou o teclado virtual a tempo";
            Q_EMIT injected(false,
                            QStringLiteral("O compositor não liberou o teclado virtual."));
        }
    });
}

EisInjector::~EisInjector()
{
    detach();
    delete d;
}

bool EisInjector::isReady() const { return d->ready; }

bool EisInjector::attach(int eisFd)
{
    detach();

    d->context = ei_new_sender(this);
    if (!d->context) {
        ::close(eisFd);
        return false;
    }
    ei_configure_name(d->context, "surr");

    // A libei toma posse do descritor a partir daqui.
    if (ei_setup_backend_fd(d->context, eisFd) != 0) {
        qCWarning(SURR_INJECT) << "ei_setup_backend_fd recusou o descritor";
        ei_unref(d->context);
        d->context = nullptr;
        return false;
    }

    d->notifier = new QSocketNotifier(ei_get_fd(d->context), QSocketNotifier::Read, this);
    connect(d->notifier, &QSocketNotifier::activated, this, &EisInjector::onReadable);

    // Os bytes do handshake podem já estar no buffer: sem esta chamada
    // manual, o notifier pode nunca disparar e a conexão fica pendurada.
    drainEvents();
    d->handshakeTimer->start(kHandshakeTimeoutMs);
    return true;
}

void EisInjector::detach()
{
    d->stepTimer->stop();
    d->handshakeTimer->stop();
    d->chordActive = false;

    // O notifier tem de parar antes do contexto: o fd que ele vigia é
    // fechado por ei_unref(). E `detach()` pode ser chamado de dentro do
    // próprio slot do notifier (no EI_EVENT_DISCONNECT), então ele é
    // desarmado e destruído depois — destruir um QObject durante a emissão
    // do seu próprio sinal é use-after-free.
    if (d->notifier) {
        d->notifier->setEnabled(false);
        disconnect(d->notifier, nullptr, this, nullptr);
        d->notifier->deleteLater();
        d->notifier = nullptr;
    }

    if (d->device) {
        ei_device_unref(d->device);
        d->device = nullptr;
    }
    if (d->seat) {
        ei_seat_unref(d->seat);
        d->seat = nullptr;
    }
    if (d->context) {
        ei_unref(d->context);
        d->context = nullptr;
    }
    if (d->ready) {
        d->ready = false;
        Q_EMIT readyChanged(false);
    }
}

void EisInjector::onReadable() { drainEvents(); }

void EisInjector::drainEvents()
{
    if (!d->context) {
        return;
    }

    ei_dispatch(d->context); // socket → fila interna

    // Esvaziar a FILA, não o socket. O QSocketNotifier avisa sobre o
    // descritor; a fila da libei é outra coisa, e parar de ler quando o fd
    // deixa de estar legível deixa eventos presos — a classe de bug que o
    // app anterior levou dias para achar com select() no python-xlib.
    while (ei_event *event = ei_get_event(d->context)) {
        switch (ei_event_get_type(event)) {
        case EI_EVENT_CONNECT:
            qCDebug(SURR_INJECT) << "EIS conectado";
            break;

        case EI_EVENT_SEAT_ADDED: {
            ei_seat *seat = ei_event_get_seat(event);
            if (ei_seat_has_capability(seat, EI_DEVICE_CAP_KEYBOARD)) {
                d->seat = ei_seat_ref(seat);
                ei_seat_bind_capabilities(seat, EI_DEVICE_CAP_KEYBOARD, nullptr);
            }
            break;
        }

        case EI_EVENT_DEVICE_ADDED: {
            ei_device *dev = ei_event_get_device(event);
            if (ei_device_has_capability(dev, EI_DEVICE_CAP_KEYBOARD)) {
                d->device = ei_device_ref(dev);
                resolveKeycodes();
            }
            break;
        }

        case EI_EVENT_DEVICE_RESUMED:
            if (d->device && ei_event_get_device(event) == d->device) {
                d->handshakeTimer->stop();
                if (!d->ready) {
                    d->ready = true;
                    qCInfo(SURR_INJECT) << "teclado virtual pronto";
                    Q_EMIT readyChanged(true);
                }
            }
            break;

        case EI_EVENT_DEVICE_PAUSED:
            // Pausado não é removido: o dispositivo volta. Mantê-lo evita
            // refazer todo o handshake a cada troca de foco.
            if (d->device && ei_event_get_device(event) == d->device && d->ready) {
                d->ready = false;
                Q_EMIT readyChanged(false);
            }
            break;

        case EI_EVENT_DEVICE_REMOVED:
            if (d->device && ei_event_get_device(event) == d->device) {
                ei_device_unref(d->device);
                d->device = nullptr;
                if (d->ready) {
                    d->ready = false;
                    Q_EMIT readyChanged(false);
                }
            }
            break;

        case EI_EVENT_DISCONNECT:
            qCWarning(SURR_INJECT) << "EIS desconectou";
            ei_event_unref(event);
            detach();
            Q_EMIT disconnected();
            return;

        default:
            break;
        }
        ei_event_unref(event);
    }
}

void EisInjector::resolveKeycodes()
{
    d->keyV = KEY_V;
    if (!d->device) {
        return;
    }

    // Cravar 47 quebraria layouts como Dvorak: a tecla de colar iria parar
    // em outro lugar, sem erro visível. O compositor entrega o keymap XKB,
    // então dá para descobrir o código real.
    ei_keymap *keymap = ei_device_keyboard_get_keymap(d->device);
    if (!keymap || ei_keymap_get_type(keymap) != EI_KEYMAP_TYPE_XKB) {
        return;
    }

    const int fd = ei_keymap_get_fd(keymap);
    const size_t size = ei_keymap_get_size(keymap);
    if (fd < 0 || size == 0) {
        return;
    }

    void *mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        return;
    }

    if (xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS)) {
        xkb_keymap *km = xkb_keymap_new_from_buffer(
            ctx, static_cast<const char *>(mapped), size - 1,
            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (km) {
            const xkb_keycode_t min = xkb_keymap_min_keycode(km);
            const xkb_keycode_t max = xkb_keymap_max_keycode(km);
            for (xkb_keycode_t kc = min; kc <= max; ++kc) {
                const xkb_keysym_t *syms = nullptr;
                if (xkb_keymap_key_get_syms_by_level(km, kc, 0, 0, &syms) == 1
                    && syms[0] == XKB_KEY_v) {
                    d->keyV = kc - 8; // keycode XKB = evdev + 8
                    break;
                }
            }
            xkb_keymap_unref(km);
        }
        xkb_context_unref(ctx);
    }
    munmap(mapped, size);
    qCDebug(SURR_INJECT) << "keycode evdev de 'v' resolvido:" << d->keyV;
}

void EisInjector::sendPaste() { beginChord(false); }
void EisInjector::sendTerminalPaste() { beginChord(true); }

void EisInjector::beginChord(bool withShift)
{
    if (d->chordActive) {
        return; // uma combinação por vez
    }
    if (!d->device || !d->ready) {
        Q_EMIT injected(false, QStringLiteral("O teclado virtual não está disponível."));
        return;
    }

    d->chordActive = true;
    d->chordShift = withShift;
    d->step = 0;
    ei_device_start_emulating(d->device, ++d->sequence);
    onStep();
}

void EisInjector::onStep()
{
    if (!d->chordActive || !d->device) {
        return;
    }

    // Sequência: Ctrl↓ [Shift↓] V↓ V↑ [Shift↑] Ctrl↑
    // Cada etapa é um frame próprio, espaçada no tempo.
    const bool shift = d->chordShift;
    const int last = shift ? 5 : 3;

    auto key = [this](uint32_t code, bool press) {
        ei_device_keyboard_key(d->device, code, press);
        ei_device_frame(d->device, ei_now(d->context)); // frame = flush
    };

    if (!shift) {
        switch (d->step) {
        case 0: key(KEY_LEFTCTRL, true); break;
        case 1: key(d->keyV, true); break;
        case 2: key(d->keyV, false); break;
        case 3: key(KEY_LEFTCTRL, false); break;
        }
    } else {
        switch (d->step) {
        case 0: key(KEY_LEFTCTRL, true); break;
        case 1: key(KEY_LEFTSHIFT, true); break;
        case 2: key(d->keyV, true); break;
        case 3: key(d->keyV, false); break;
        case 4: key(KEY_LEFTSHIFT, false); break;
        case 5: key(KEY_LEFTCTRL, false); break;
        }
    }

    if (d->step >= last) {
        ei_device_stop_emulating(d->device);
        d->chordActive = false;
        Q_EMIT injected(true, QString());
        return;
    }

    ++d->step;
    // QTimer, nunca sleep: dormir na thread principal travaria o D-Bus,
    // o medidor de nível e a máquina de estados.
    d->stepTimer->start();
}

} // namespace surr
