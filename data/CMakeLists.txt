set(SURR_BIN "${KDE_INSTALL_FULL_BINDIR}/surr")
configure_file(br.com.cepify.Surr.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/br.com.cepify.Surr.desktop @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/br.com.cepify.Surr.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})

install(FILES br.com.cepify.Surr.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})

# Ícone escalável: a bandeja e o menu resolvem pelo nome do app-id.
install(FILES icons/br.com.cepify.Surr.svg
        DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/scalable/apps)

# Configuração das notificações do KNotification.
install(FILES surr.notifyrc
        DESTINATION ${KDE_INSTALL_DATAROOTDIR}/knotifications6)
