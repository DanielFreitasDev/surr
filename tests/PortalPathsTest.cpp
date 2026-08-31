#include "PortalPaths.h"

#include <QRegularExpression>
#include <QTest>

using namespace surr;

class PortalPathsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sanitizesBusName_data();
    void sanitizesBusName();
    void buildsRequestPath();
    void tokensAreValidPathElements();
};

void PortalPathsTest::sanitizesBusName_data()
{
    QTest::addColumn<QString>("bus");
    QTest::addColumn<QString>("expected");

    // ":1.234" → "1_234": tira o ':' inicial e troca '.' por '_'.
    QTest::newRow("típico") << QStringLiteral(":1.234") << QStringLiteral("1_234");
    QTest::newRow("sem dois-pontos") << QStringLiteral("1.234") << QStringLiteral("1_234");
    QTest::newRow("vários pontos") << QStringLiteral(":1.2.3") << QStringLiteral("1_2_3");
}

void PortalPathsTest::sanitizesBusName()
{
    QFETCH(QString, bus);
    QFETCH(QString, expected);
    QCOMPARE(PortalPaths::sanitizeBusName(bus), expected);
}

void PortalPathsTest::buildsRequestPath()
{
    // Prever este caminho é o que permite conectar o Response ANTES da
    // chamada. Errar aqui trava o aplicativo esperando um sinal que já passou.
    QCOMPARE(PortalPaths::requestPath(QStringLiteral(":1.42"), QStringLiteral("tok1")),
             QStringLiteral("/org/freedesktop/portal/desktop/request/1_42/tok1"));
}

void PortalPathsTest::tokensAreValidPathElements()
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_]+$"));
    QSet<QString> seen;
    for (int i = 0; i < 200; ++i) {
        const QString token = PortalPaths::makeToken("req");
        QVERIFY2(valid.match(token).hasMatch(), qPrintable(token));
        seen.insert(token);
    }
    // Tokens repetidos fariam duas chamadas simultâneas escutarem o mesmo
    // caminho de Request e uma consumiria a resposta da outra.
    QCOMPARE(seen.size(), 200);
}

QTEST_MAIN(PortalPathsTest)
#include "PortalPathsTest.moc"
