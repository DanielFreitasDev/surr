include(ECMAddTests)

ecm_add_test(WavWriterTest.cpp
    TEST_NAME wavwriter
    LINK_LIBRARIES surr_core Qt6::Test)

ecm_add_test(DictationStateMachineTest.cpp
    TEST_NAME statemachine
    LINK_LIBRARIES surr_core Qt6::Test)

ecm_add_test(ProviderTest.cpp
    TEST_NAME provider
    LINK_LIBRARIES surr_core Qt6::Test)

ecm_add_test(PortalPathsTest.cpp
    TEST_NAME portalpaths
    LINK_LIBRARIES surr_core Qt6::Test)

ecm_add_test(PcmConverterTest.cpp
    TEST_NAME pcmconverter
    LINK_LIBRARIES surr_core Qt6::Test)

ecm_add_test(ProxyResolverTest.cpp
    TEST_NAME proxyresolver
    LINK_LIBRARIES surr_core Qt6::Test)

# Precisa do surr_platform: Settings usa KConfig.
ecm_add_test(SettingsTest.cpp
    TEST_NAME settings
    LINK_LIBRARIES surr_platform Qt6::Test)
