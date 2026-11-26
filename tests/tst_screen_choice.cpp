#include "screenchoice.h"

#include <QTest>

using lumen::render::chooseOutputScreen;
using lumen::render::ScreenChoice;

Q_DECLARE_METATYPE(ScreenChoice)

namespace QTest {
template <>
char *toString(const ScreenChoice &c)
{
    return qstrdup(QByteArray("{index " + QByteArray::number(c.index)
                              + (c.fullscreen ? ", fullscreen}" : ", windowed}")));
}
} // namespace QTest

// Where the output window goes for each screen setup.
class TestScreenChoice : public QObject
{
    Q_OBJECT

private slots:
    void choose_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("primary");
        QTest::addColumn<int>("requested"); // -1: none
        QTest::addColumn<ScreenChoice>("expected");

        QTest::newRow("no screens") << 0 << -1 << -1 << ScreenChoice{-1, false};
        QTest::newRow("laptop only: window, never cover the UI") << 1 << 0 << -1 << ScreenChoice{0, false};
        QTest::newRow("projector is screen 1") << 2 << 0 << -1 << ScreenChoice{1, true};
        QTest::newRow("projector is screen 0") << 2 << 1 << -1 << ScreenChoice{0, true};
        QTest::newRow("three screens: first non-primary") << 3 << 1 << -1 << ScreenChoice{0, true};
        QTest::newRow("requested screen is used") << 3 << 0 << 2 << ScreenChoice{2, true};
        QTest::newRow("requesting the primary is honoured") << 2 << 0 << 0 << ScreenChoice{0, true};
        QTest::newRow("requested out of range falls back") << 2 << 0 << 5 << ScreenChoice{1, true};
        QTest::newRow("negative request falls back") << 1 << 0 << -3 << ScreenChoice{0, false};
        QTest::newRow("unknown primary treated as 0") << 2 << -1 << -1 << ScreenChoice{1, true};
    }

    void choose()
    {
        QFETCH(int, count);
        QFETCH(int, primary);
        QFETCH(int, requested);
        QFETCH(ScreenChoice, expected);
        const std::optional<int> request = requested == -1 ? std::nullopt : std::optional<int>(requested);
        QCOMPARE(chooseOutputScreen(count, primary, request), expected);
    }
};

QTEST_APPLESS_MAIN(TestScreenChoice)
#include "tst_screen_choice.moc"
