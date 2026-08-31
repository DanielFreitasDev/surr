#include "Settings.h"

#include <QStandardPaths>
#include <QTest>

using namespace surr;

/**
 * Os testes escrevem configuração de verdade, então o QStandardPaths é
 * redirecionado para um diretório temporário — do contrário a suíte
 * sobrescreveria o `surrrc` do usuário que a rodou.
 */
class SettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void defaultsAreUsable();
    void roundTripsValues();
    void persistsAcrossInstances();
    void languageEmptyMeansAutomatic();

private:
    void wipe()
    {
        Settings s;
        s.setProvider(QStringLiteral("groq"));
        s.setLanguage(QStringLiteral("pt"));
        s.setGroqModel(QStringLiteral("whisper-large-v3-turbo"));
        s.setMinimumHoldMs(450);
        s.setAutoPaste(true);
        s.save();
    }
};

void SettingsTest::defaultsAreUsable()
{
    Settings s;
    // Um aplicativo recém-instalado precisa estar utilizável sem o usuário
    // abrir as configurações: serviço, modelo, idioma e colagem já definidos.
    QCOMPARE(s.provider(), QStringLiteral("groq"));
    QCOMPARE(s.groqModel(), QStringLiteral("whisper-large-v3-turbo"));
    QCOMPARE(s.preferredTrigger(), QStringLiteral("Pause"));
    QVERIFY(s.autoPaste());
    QCOMPARE(s.minimumHoldMs(), 450);
    QCOMPARE(s.pasteDelayMs(), 90);
    QVERIFY(!s.onboardingDone());
    // O áudio não deve ficar em disco por padrão.
    QVERIFY(!s.debugKeepAudio());
    QCOMPARE(s.cepifyBaseUrl(), QStringLiteral("https://whisper.exemplo.com.br"));
}

void SettingsTest::roundTripsValues()
{
    Settings s;
    s.setProvider(QStringLiteral("cepify"));
    s.setGroqModel(QStringLiteral("whisper-large-v3"));
    s.setLanguage(QStringLiteral("en"));
    s.setMinimumHoldMs(600);
    s.setAutoPaste(false);
    s.setTerminalPasteMode(true);
    s.setHotwords(QStringLiteral("Cepify, Fortaleza"));

    QCOMPARE(s.provider(), QStringLiteral("cepify"));
    QCOMPARE(s.groqModel(), QStringLiteral("whisper-large-v3"));
    QCOMPARE(s.language(), QStringLiteral("en"));
    QCOMPARE(s.minimumHoldMs(), 600);
    QVERIFY(!s.autoPaste());
    QVERIFY(s.terminalPasteMode());
    QCOMPARE(s.hotwords(), QStringLiteral("Cepify, Fortaleza"));
    wipe();
}

void SettingsTest::persistsAcrossInstances()
{
    {
        Settings s;
        s.setLanguage(QStringLiteral("es"));
        s.setMinimumHoldMs(321);
        s.save();
    }
    {
        Settings s;
        QCOMPARE(s.language(), QStringLiteral("es"));
        QCOMPARE(s.minimumHoldMs(), 321);
    }
    wipe();
}

void SettingsTest::languageEmptyMeansAutomatic()
{
    Settings s;
    s.setLanguage(QString());
    // Vazio é significativo: o parâmetro `language` é omitido da requisição,
    // e o modelo detecta sozinho. Não pode virar um valor padrão silencioso.
    QVERIFY(s.language().isEmpty());
    wipe();
}

QTEST_MAIN(SettingsTest)
#include "SettingsTest.moc"
