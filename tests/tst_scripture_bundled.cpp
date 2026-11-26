#include "operatorharness.h"

#include "lumen/core/scripture/biblestore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace lumen;
using namespace lumen::core::scripture;
using lumen::test::OperatorHarness;

// Batch 7 "Done when": "jn 3 16-18" shows the right verses in every bundled
// Bible.
//
// Every translation in bibles/manifest.json must be in the built bibles.db.
// For each, "jn 3 16-18" must give John 3:16, 17 and 18, labelled with the
// translation's own book name, starting with that translation's published
// wording of John 3:16, and with its copyright line. Then the same is done
// through the real window: Ctrl+B, type, pick the translation, Enter.
class TestScriptureBundled : public QObject
{
    Q_OBJECT

private:
    struct Expected
    {
        QString book;  // the translation's name for John
        QString start; // how its John 3:16 begins
    };
    QHash<QString, Expected> m_expected{
        {"engwebp", {"John", "For God so loved the world, that he gave his only born Son"}},
        {"eng-kjv2006", {"John", "For God so loved the world, that he gave his only begotten Son"}},
        {"engbsb", {"John", "For God so loved the world that He gave His one and only Son"}},
        {"eng-asv", {"John", "For God so loved the world, that he gave his only begotten Son"}},
        {"yor", {"Johanu", "“Nítorí Ọlọ́run fẹ́ aráyé tó bẹ́ẹ̀ gẹ́ẹ́"}},
        {"hausa", {"Yohanna", "Gama Allah ya ƙaunaci duniya sosai"}},
        {"ibo", {"Jọn", "Nʼihi na Chineke hụrụ ụwa nʼanya"}},
        {"swhonmm", {"Yohana", "“Kwa maana jinsi hii Mungu aliupenda ulimwengu"}},
        {"fraLSG", {"Jean", "Car Dieu a tant aimé le monde qu’il a donné son Fils unique"}},
        {"spaRV1909", {"Juan", "Porque de tal manera amó Dios al mundo, que ha dado á su Hijo unigénito"}},
    };

    QJsonArray m_manifest;
    std::unique_ptr<BibleStore> m_store;
    BookNames m_names;

    QJsonObject manifestEntry(const QString &id) const
    {
        for (const QJsonValue &value : m_manifest) {
            if (value.toObject().value("id").toString() == id)
                return value.toObject();
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QFile manifest(QStringLiteral(LUMEN_BIBLES_MANIFEST));
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        m_manifest = QJsonDocument::fromJson(manifest.readAll()).object().value("translations").toArray();
        QCOMPARE(m_manifest.size(), 10);

        auto store = BibleStore::open(QStringLiteral(LUMEN_BIBLES_DB), true);
        QVERIFY2(store.ok(), store.ok() ? "" : qPrintable(store.error().message));
        m_store = std::move(store).value();
        QVERIFY(m_store->addBookNames(m_names).ok());
    }

    void everyManifestTranslationIsBundled()
    {
        const auto translations = m_store->translations();
        QVERIFY(translations.ok());
        QStringList bundled;
        for (const Translation &t : translations.value())
            bundled << t.id;
        QStringList listed;
        for (const QJsonValue &value : m_manifest)
            listed << value.toObject().value("id").toString();
        QCOMPARE(bundled, listed); // same set, same order as the buttons
        for (const Translation &t : translations.value()) {
            const QJsonObject entry = manifestEntry(t.id);
            QCOMPARE(t.copyright, entry.value("copyright").toString());
            QCOMPARE(t.licence, entry.value("licence").toString());
            QVERIFY2(!t.notice.isEmpty(), qPrintable(t.id + ": the package's copyright page is missing"));
        }
    }

    void john3v16to18_data()
    {
        QTest::addColumn<QString>("id");
        for (const QJsonValue &value : m_manifest)
            QTest::newRow(qPrintable(value.toObject().value("id").toString())) << value.toObject().value("id").toString();
    }

    void john3v16to18()
    {
        QFETCH(QString, id);
        QVERIFY2(m_expected.contains(id), qPrintable("no expectation written for " + id));
        const Expected expected = m_expected.value(id);

        auto reference = parseReference(QStringLiteral("jn 3 16-18"), m_names);
        QVERIFY(reference.ok());
        auto passage = m_store->passage(id, reference.value());
        QVERIFY2(passage.ok(), passage.ok() ? "" : qPrintable(passage.error().message));
        const Passage &p = passage.value();

        QCOMPARE(p.label, expected.book + QStringLiteral(" 3:16–18"));
        QCOMPARE(p.verses.size(), std::size_t(3));
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(p.verses[std::size_t(i)].book, QStringLiteral("JHN"));
            QCOMPARE(p.verses[std::size_t(i)].chapter, 3);
            QCOMPARE(p.verses[std::size_t(i)].verse, 16 + i);
            QVERIFY(p.verses[std::size_t(i)].text.size() > 20);
            QVERIFY(!p.verses[std::size_t(i)].text.contains(u'\\')); // no markup left
            QVERIFY(!p.verses[std::size_t(i)].text.contains(u'|'));
        }
        QVERIFY2(p.verses[0].text.startsWith(expected.start), qPrintable(p.verses[0].text));
        QVERIFY(p.verses[0].text != p.verses[1].text);
        QCOMPARE(p.translation.copyright, manifestEntry(id).value("copyright").toString());

        // The translation's own spelling of the book works too.
        auto own = parseReference(expected.book + QStringLiteral(" 3:16-18"), m_names);
        QVERIFY(own.ok());
        QCOMPARE(own.value(), reference.value());
    }

    // Set LUMEN_SCREENSHOT_DIR to save the panel as the mockup shows it.
    void screenshot()
    {
        const QString dir = qEnvironmentVariable("LUMEN_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("LUMEN_SCREENSHOT_DIR not set");
        OperatorHarness ui;
        ui.bibles = m_store.get();
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QTest::keyClick(ui.window, Qt::Key_B, Qt::ControlModifier);
        for (const char c : QByteArray("rom 8 28-30"))
            QTest::keyClick(ui.window, c);
        ui.settle();
        QTest::qWait(100);
        QVERIFY(ui.window->grabWindow().save(dir + QStringLiteral("/scripture.png")));
    }

    // The same through the operator window, for every translation.
    void quickSearchInTheWindow()
    {
        OperatorHarness ui;
        ui.bibles = m_store.get();
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));

        for (const QJsonValue &value : m_manifest) {
            const QString id = value.toObject().value("id").toString();
            const Expected expected = m_expected.value(id);

            // Scripture is a window of its own (it stays open beside the
            // operator window); Ctrl+B opens it or brings it forward.
            // Back in the operator window (as clicking it would), Ctrl+B.
            ui.window->requestActivate();
            QTRY_VERIFY(ui.window->activeFocusItem());
            QTest::keyClick(ui.window, Qt::Key_B, Qt::ControlModifier);
            QTRY_VERIFY(ui.item("scriptureDialog"));
            QQuickItem *dialog = ui.item("scriptureDialog");
            QQuickWindow *scriptureWindow = dialog->window();
            QVERIFY(scriptureWindow != ui.window);
            QTRY_VERIFY(scriptureWindow->isVisible());
            QQuickItem *input = ui.item("scriptureReference");
            QTRY_VERIFY(input->hasActiveFocus());

            // Type it as an operator would.
            for (const char c : QByteArray("jn 3 16-18"))
                QTest::keyClick(scriptureWindow, c);
            QVERIFY(ui.click("translation-" + id));
            QCOMPARE(ui.scripture->label(), expected.book + QStringLiteral(" 3:16–18"));
            QCOMPARE(ui.scripture->verses().size(), 3);
            QTRY_VERIFY(ui.item("verse-2"));
            QCOMPARE(ui.item("scriptureCopyright")->property("text").toString(),
                     value.toObject().value("copyright").toString());

            // Enter: in Preview, not yet on the screen (the user's request:
            // "choosing a scripture should take it to the preview window
            // first"); Shift+Enter: on the screen. The window stays open.
            const QString liveBefore = ui.controller->liveText();
            QTest::keyClick(scriptureWindow, Qt::Key_Return);
            QTRY_VERIFY2(ui.controller->previewText().startsWith(expected.start), qPrintable(ui.controller->previewText()));
            QCOMPARE(ui.controller->liveText(), liveBefore);
            QTest::keyClick(scriptureWindow, Qt::Key_Return, Qt::ShiftModifier);
            QVERIFY(scriptureWindow->isVisible());
            QVERIFY2(ui.controller->liveText().startsWith(expected.start), qPrintable(ui.controller->liveText()));
            // Batch 8: the reference and the translation's copyright line are
            // the slide's footer (they ended the slide text in batch 7).
            const auto &live = ui.show.scene().layer(lumen::core::show::Layer::Slide).content;
            QCOMPARE(live.footer, QStringLiteral("%1 3:16 (%2) · %3")
                                      .arg(expected.book, value.toObject().value("abbreviation").toString(),
                                           value.toObject().value("copyright").toString()));
            QVERIFY(!live.style.isEmpty()); // drawn with the scripture theme
            QCOMPARE(ui.controller->slides()->property("count").toInt(), 3);
        }
    }
};

QTEST_MAIN(TestScriptureBundled)
#include "tst_scripture_bundled.moc"
