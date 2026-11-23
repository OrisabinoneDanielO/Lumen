#include "lumen/core/buildinfo.h"

#include <QRegularExpression>
#include <QTest>

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void appNameIsLumenPresenter()
    {
        QCOMPARE(lumen::core::appName(), QStringLiteral("Lumen Presenter"));
    }

    void versionMatchesCMakeProject()
    {
        QCOMPARE(lumen::core::version(), QStringLiteral(EXPECTED_VERSION));
    }

    void versionIsSemantic()
    {
        static const QRegularExpression semver(QStringLiteral(R"(^\d+\.\d+\.\d+$)"));
        QVERIFY2(semver.match(lumen::core::version()).hasMatch(),
                 qPrintable(lumen::core::version()));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
