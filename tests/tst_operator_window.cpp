#include "operatorharness.h"

#include <QRegularExpression>
#include <QTest>

using namespace lumen;
using namespace lumen::core::show;
using lumen::test::OperatorHarness;

// The operator window driven like an operator would: mouse clicks and keys.
class TestOperatorWindow : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<OperatorHarness> m_ui;

    QString onAir() const
    {
        const Content &content = m_ui->show.scene().layer(Layer::Slide).content;
        return content.kind == Content::Kind::Text ? content.text : QString();
    }

    void open(bool editAvailable = false)
    {
        m_ui = std::make_unique<OperatorHarness>();
        QString error;
        QVERIFY2(m_ui->load(editAvailable, &error), qPrintable(error));
    }

private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    }

    void cleanup() { m_ui.reset(); }

    void clickingAPlanItemShowsItsSlides()
    {
        open();
        QVERIFY(m_ui->click("planItem-2"));
        QCOMPARE(m_ui->controller->currentItem(), 2);
        QCOMPARE(m_ui->controller->itemTitle(), QStringLiteral("Amazing Grace"));
        QTRY_VERIFY(m_ui->item("slide-8"));
        QCOMPARE(m_ui->show.scene().sequence, quint64(0)); // nothing went live
    }

    void clickingASlideSendsItLive()
    {
        open();
        m_ui->click("planItem-2");
        QTRY_VERIFY(m_ui->item("slide-2"));
        QVERIFY(m_ui->click("slide-2"));
        QVERIFY(onAir().startsWith(QLatin1String("'Twas grace")));
        QTRY_COMPARE(m_ui->item("slide-2")->property("status").toString(), QStringLiteral("live"));
        QCOMPARE(m_ui->item("slide-3")->property("status").toString(), QStringLiteral("next"));
        QTRY_COMPARE(m_ui->item("liveFrame")->property("text").toString(), onAir());
    }

    void keysDriveTheShow()
    {
        open();
        m_ui->click("planItem-2");
        m_ui->key(Qt::Key_Space);
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));
        m_ui->key(Qt::Key_Right);
        QVERIFY(onAir().startsWith(QLatin1String("I once was lost")));
        m_ui->key(Qt::Key_Left);
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));
        m_ui->key(Qt::Key_V);
        m_ui->key(Qt::Key_3);
        QVERIFY(onAir().startsWith(QLatin1String("Through many dangers")));
        m_ui->key(Qt::Key_F7);
        QVERIFY(m_ui->controller->blackOn());
        m_ui->key(Qt::Key_F7);
        m_ui->key(Qt::Key_PageDown);
        QCOMPARE(m_ui->controller->currentItem(), 3);
    }

    void toolbarButtonsWork()
    {
        open();
        m_ui->click("planItem-2");
        m_ui->key(Qt::Key_Space);
        QVERIFY(m_ui->click("black"));
        QVERIFY(m_ui->show.scene().black);
        QVERIFY(m_ui->click("black"));
        QVERIFY(m_ui->click("logo"));
        QVERIFY(m_ui->show.scene().logo);
        QVERIFY(m_ui->click("clearSlide"));
        QVERIFY(onAir().isEmpty());
        QVERIFY(m_ui->click("nextSlide"));
        QVERIFY(onAir().startsWith(QLatin1String("I once was lost")));
        QVERIFY(m_ui->click("previousSlide"));
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));
        QVERIFY(m_ui->click("clearAll"));
        QVERIFY(onAir().isEmpty());
    }

    // Typing in the library search must never change the screen.
    void shortcutsPauseWhileSearching()
    {
        open();
        m_ui->click("planItem-2");
        QVERIFY(m_ui->click("librarySearch"));
        QTRY_VERIFY(m_ui->item("searchInput")->hasActiveFocus());
        for (const char c : QByteArray("grace c 3"))
            QTest::keyClick(m_ui->window, c);
        m_ui->key(Qt::Key_F7);
        QCOMPARE(m_ui->show.scene().sequence, quint64(0));
        QCOMPARE(m_ui->item("searchInput")->property("text").toString(), QStringLiteral("grace c 3"));

        m_ui->key(Qt::Key_Escape); // back to the show
        QTRY_VERIFY(!m_ui->item("searchInput")->hasActiveFocus());
        m_ui->key(Qt::Key_Space);
        QVERIFY(onAir().startsWith(QLatin1String("Amazing grace!")));
    }

    void editModeSwitchAndLockout()
    {
        open(true);
        m_ui->click("planItem-2");
        m_ui->key(Qt::Key_Space);
        const quint64 sequence = m_ui->show.scene().sequence;

        QVERIFY(m_ui->item("editMode")->isVisible());
        QVERIFY(m_ui->click("editMode"));
        QCOMPARE(m_ui->controller->mode(), QStringLiteral("edit"));
        QTRY_VERIFY(m_ui->item("editor")->isVisible());
        QVERIFY(!m_ui->item("slideGrid")->isVisible());

        QTRY_VERIFY(m_ui->item("editorSlide-2"));
        QVERIFY(m_ui->click("editorSlide-2")); // in view in the strip
        QCOMPARE(m_ui->controller->selectedSlide(), 2);
        m_ui->key(Qt::Key_Space);
        QCOMPARE(m_ui->show.scene().sequence, sequence); // nothing reached the screen

        QVERIFY(m_ui->click("showMode"));
        QTRY_VERIFY(m_ui->item("slideGrid")->isVisible());
        m_ui->key(Qt::Key_Space);
        QVERIFY(onAir().startsWith(QLatin1String("I once was lost")));
    }
};

QTEST_MAIN(TestOperatorWindow)
#include "tst_operator_window.moc"
