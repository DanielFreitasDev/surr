cmake_minimum_required(VERSION 3.22)

project(surr
    VERSION 0.1.0
    DESCRIPTION "Ditado por voz push-to-talk para KDE Plasma 6 / Wayland"
    LANGUAGES CXX)

set(SURR_APP_ID "br.com.cepify.Surr" CACHE STRING "Application ID (reverse-DNS)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMDeprecationSettings)
include(FeatureSummary)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core Gui Widgets Network DBus Qml Quick QuickControls2 Multimedia)
find_package(Qt6 ${QT_MIN_VERSION} QUIET COMPONENTS Test)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config CoreAddons I18n Wallet StatusNotifierItem Notifications
    DBusAddons WindowSystem IconThemes ColorScheme KIO)

find_package(LayerShellQt QUIET)
set_package_properties(LayerShellQt PROPERTIES
    TYPE RECOMMENDED
    PURPOSE "Posiciona a cápsula de gravação sem roubar foco no Wayland")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEI REQUIRED IMPORTED_TARGET libei-1.0)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
add_feature_info(libei LIBEI_FOUND "Colagem automática via portal RemoteDesktop + EIS")

ecm_set_disabled_deprecation_versions(QT 6.6 KF 6.0)

add_subdirectory(src)
add_subdirectory(data)

if(Qt6Test_FOUND)
    enable_testing()
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
