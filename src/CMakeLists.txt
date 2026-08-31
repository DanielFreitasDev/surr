configure_file(core/surr_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/surr_config.h @ONLY)

# ---------------------------------------------------------------------------
# surr_core — lógica sem GUI.
#
# Tudo aqui roda sem sessão gráfica, e é por isso que existe como biblioteca
# separada: os testes do ctest exercitam a máquina de estados, o WAV e os
# providers sem precisar de Wayland.
# ---------------------------------------------------------------------------
add_library(surr_core STATIC
    core/SurrDebug.cpp
    core/WavWriter.cpp
    core/DictationStateMachine.cpp
    core/ProviderErrors.cpp
    core/OpenAiCompatibleProvider.cpp
    core/GroqProvider.cpp
    core/CepifyProvider.cpp
    core/PortalPaths.cpp
    core/PcmConverter.cpp
    core/ProxyResolver.cpp

    # Cabeçalhos com Q_OBJECT precisam constar aqui: o AUTOMOC só gera o moc
    # de um header se ele estiver na lista de fontes ou casar com um .cpp
    # homônimo. ITranscriptionProvider é puramente abstrato e não tem .cpp.
    core/ITranscriptionProvider.h
    core/OpenAiCompatibleProvider.h
    core/GroqProvider.h
    core/CepifyProvider.h
    core/DictationStateMachine.h
)

target_include_directories(surr_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(surr_core PUBLIC
    Qt6::Core
    Qt6::Network
)

set_target_properties(surr_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# surr_platform — precisa do barramento de sessão, mas não de display.
#
# A separação é enforcement, não decoração: como surr_core não linka
# Qt6::DBus, um #include <QDBusMessage> perdido na máquina de estados vira
# erro de compilação, e a suíte headless continua headless.
# ---------------------------------------------------------------------------
add_library(surr_platform STATIC
    platform/portal/PortalTypes.cpp
    platform/portal/PortalRequest.cpp
    platform/portal/RemoteDesktopSession.cpp
    platform/portal/ClipboardManager.cpp
    platform/portal/GlobalShortcutManager.cpp
    platform/input/EisInjector.cpp
    platform/audio/AudioEngine.cpp
    platform/audio/AudioRecorder.cpp
    platform/secrets/SecretStore.cpp
    platform/Settings.cpp
    platform/DeliveryCoordinator.cpp

    platform/portal/PortalTypes.h
    platform/portal/PortalRequest.h
    platform/portal/RemoteDesktopSession.h
    platform/portal/ClipboardManager.h
    platform/portal/GlobalShortcutManager.h
    platform/input/EisInjector.h
    platform/audio/AudioEngine.h
    platform/audio/AudioRecorder.h
    platform/secrets/SecretStore.h
    platform/Settings.h
    platform/DeliveryCoordinator.h
)

target_include_directories(surr_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/portal
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/input
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/secrets
)

target_link_libraries(surr_platform PUBLIC
    surr_core
    Qt6::DBus
    PkgConfig::LIBEI
    PkgConfig::XKBCOMMON
    Qt6::Multimedia
    KF6::Wallet
    KF6::ConfigCore
    Qt6::Gui
)

# ---------------------------------------------------------------------------
# Ferramenta de diagnóstico: valida os portais no ambiente real.
# ---------------------------------------------------------------------------
add_executable(surr-probe ${CMAKE_SOURCE_DIR}/tools/surr-probe.cpp)
target_link_libraries(surr-probe PRIVATE surr_platform)

add_executable(surr-probe-paste ${CMAKE_SOURCE_DIR}/tools/surr-probe-paste.cpp)
target_link_libraries(surr-probe-paste PRIVATE surr_platform)

add_executable(surr-probe-audio ${CMAKE_SOURCE_DIR}/tools/surr-probe-audio.cpp)
target_link_libraries(surr-probe-audio PRIVATE surr_platform)

add_executable(surr-probe-transcribe ${CMAKE_SOURCE_DIR}/tools/surr-probe-transcribe.cpp)
target_link_libraries(surr-probe-transcribe PRIVATE surr_platform)

# ---------------------------------------------------------------------------
# surr_ui — precisa de display.
# ---------------------------------------------------------------------------
qt_add_library(surr_ui STATIC
    ui/OverlayController.cpp
    ui/TrayIcon.cpp
    ui/SettingsWindow.cpp
    ui/ApplicationController.cpp

    ui/OverlayController.h
    ui/TrayIcon.h
    ui/SettingsWindow.h
    ui/ApplicationController.h
)

qt_add_qml_module(surr_ui
    URI surr
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Overlay.qml
        qml/Settings.qml
)

target_include_directories(surr_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ui)

target_link_libraries(surr_ui PUBLIC
    surr_platform
    Qt6::Gui
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    KF6::StatusNotifierItem
    KF6::Notifications
    KF6::I18n
    KF6::CoreAddons
)
if(LayerShellQt_FOUND)
    target_link_libraries(surr_ui PUBLIC LayerShellQt::Interface)
endif()

# ---------------------------------------------------------------------------
# Executável
# ---------------------------------------------------------------------------
add_executable(surr main.cpp)
target_link_libraries(surr PRIVATE surr_ui KF6::DBusAddons)

install(TARGETS surr ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
