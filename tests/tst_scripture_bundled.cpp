#include "operatorharness.h"

#include "lumen/core/scripture/biblestore.h"

#include <QFile>
#include <QUrl>
#include <QTemporaryDir>
#include <QDir>
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
        // Added 2026-11-27 (bibles/README.md).
        {"engBBE", {"John", "For God had such love for"}},
        {"engDBY", {"John", "For God so loved the world,"}},
        {"engDRA", {"John", "For God so loved the world,"}},
        {"enggnv", {"John", "For God so loued the worlde,"}},
        {"engylt", {"John", "for God did so love the"}},
        {"engwebster", {"John", "For God so loved the world,"}},
        {"eng-rv", {"John", "For God so loved the world,"}},
        {"engmsb", {"John", "For God so loved the world"}},
        {"englsv", {"John", "for God so loved the world"}},
        {"engULB", {"John", "For God so loved the world,"}},
        {"engfbv", {"John", "For God loved the world, and"}},
        {"twi", {"Yohane", "“Na sɛnea Onyankopɔn dɔɔ wiase ni,"}},
        {"twiasante", {"Yohane", "“Na sɛdeɛ Onyankopɔn dɔɔ ewiase nie;"}},
        {"ewe", {"Yohanes", "Elabena Mawu lɔ̃a xexea me ale"}},
        {"swhonen", {"Yohana", "“Kwa maana jinsi hii Mungu aliupenda"}},
        {"lin", {"Yoane", "Pamba te Nzambe alingaki mokili mingi"}},
        {"lug", {"Yokaana", "“Kubanga Katonda bwe yayagala ensi, bw’atyo"}},
        {"nya", {"Yohane", "“Pakuti Mulungu anakonda dziko lapansi, kotero"}},
        {"sna", {"Johani", "“Nokuti Mwari akada nyika nokudaro, kuti"}},
        {"kik", {"Johana", "“Nĩgũkorwo Ngai nĩendire kĩrĩndĩ, akĩruta Mũrũ"}},
        {"luo", {"Johana", "“Nikech Nyasaye nohero piny ahinya, omiyo"}},
        {"nde", {"UJohane", "UNkulunkulu walithanda ilizwe kangaka waze wanikela"}},
        {"porbrbsl", {"João", "Porque Deus amou o mundo de"}},
        {"deu1912", {"Johannes", "Also hat Gott die Welt geliebt,"}},
        {"frajnd", {"Jean", "Car Dieu a tant aimé le"}},
        {"cmn-cu89s", {"约翰福音", "「 神爱世人，甚至将他的独生子赐给他们"}},
        {"cmn-cu89t", {"約翰福音", "「上帝愛世人，甚至將他的獨生子賜給他們"}},
        {"kor", {"요한복음", "하나님이 세상을 이처럼 사랑하사 독생자를 주셨으니"}},
        {"russyn", {"Евангелие от Иоанна", "Ибо так возлюбил Бог мир, что"}},
        {"hin2017", {"यूहन्ना", "“क्योंकि परमेश्वर ने जगत से ऐसा"}},
        {"ita1927", {"Giovanni", "Poiché Iddio ha tanto amato il"}},
        {"nld", {"Johannes", "Want alzo lief heeft God de"}},
        {"vie1934", {"Giăng", "Vì Đức Chúa Trời yêu thương"}},
        {"mal", {"യോഹന്നാന്‍", "തന്‍റെ ഏകജാതനായ പുത്രനിൽ വിശ്വസിക്കുന്ന"}},
    };

    QJsonArray m_manifest;
    std::unique_ptr<BibleStore> m_store;
    BookNames m_names;

    // A translation's button, or (not among the eight quick ones) its row
    // in All translations, found by typing its title in the filter.
    static bool chooseTranslation(OperatorHarness &ui, const QString &id, const QString &title)
    {
        ui.settle();
        QQuickItem *chip = ui.item("translation-" + id);
        if (chip && chip->isVisible())
            return ui.click("translation-" + id);
        if (!ui.click("translationMore"))
            return false;
        QQuickItem *list = ui.item("translationList");
        if (!list || !QTest::qWaitFor([&] { return list->isVisible(); }, 2000))
            return false;
        // As typed (titles may be in any script).
        QQuickItem *filter = ui.item("translationFilter");
        if (!filter || !filter->setProperty("text", title))
            return false;
        if (!QTest::qWaitFor([&] { return ui.item("translationItem-" + id) != nullptr; }, 2000))
            return false;
        ui.settle();
        return ui.click("translationItem-" + id);
    }

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
        QCOMPARE(m_manifest.size(), 44); // 10 from batch 7, 34 added 2026-11-27

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
        QTRY_VERIFY(ui.item("scriptureDialog"));
        QQuickWindow *window = ui.item("scriptureDialog")->window(); // its own window
        for (const char c : QByteArray("rom 8 28-30"))
            QTest::keyClick(window, c);
        ui.settle();
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(dir + QStringLiteral("/scripture.png")));
        // A new passage: Up to the book, "j" fills in John.
        QTest::keyClick(window, Qt::Key_Up);
        QTest::keyClick(window, 'j');
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(dir + QStringLiteral("/scripture-typing.png")));
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
            QVERIFY2(chooseTranslation(ui, id, value.toObject().value("title").toString()), qPrintable(id));
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

    // The reference stays in three parts: "j" fills in John (the rest
    // highlighted); Space goes on to the chapter and the verse; Left goes
    // back a part at a time, each highlighted to type over; Up goes to the
    // book. Enter: Preview; Shift+Enter: on the screen.
    // design/mockups/Scripture.html: a second translation side by side,
    // Fit to slide, and where the reference goes; kept when added to the plan.
    void slideOptions()
    {
        using lumen::core::show::Content;
        using lumen::core::show::Layer;
        OperatorHarness ui;
        ui.bibles = m_store.get();
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QTest::keyClick(ui.window, Qt::Key_B, Qt::ControlModifier);
        QTRY_VERIFY(ui.item("scriptureDialog"));
        QVERIFY(ui.click("translation-engwebp"));
        app::ScriptureController &scripture = *ui.scripture;
        scripture.lookup(QStringLiteral("jn 3 16-18"));
        QCOMPARE(scripture.item().slides.size(), std::size_t(3));
        QCOMPARE(scripture.firstSlideVerses(), 1);

        // Fit to slide: three short verses share a slide.
        QVERIFY(ui.click("versesPerSlide-0"));
        QCOMPARE(scripture.versesPerSlide(), 0);
        QVERIFY(scripture.item().slides.size() < 3);
        QVERIFY(scripture.firstSlideVerses() >= 2);
        QVERIFY(scripture.item().slides.front().text.contains(QStringLiteral("17 ")));

        // The reference above the text, then the lower third.
        const auto liveStyle = [&] { return ui.show.scene().layer(Layer::Slide).content.style; };
        QVERIFY(ui.click("referencePosition-above"));
        QVERIFY(scripture.showNow());
        QCOMPARE(liveStyle().value("footerPosition").toString(), QStringLiteral("above"));
        QVERIFY(ui.click("referencePosition-lowerThird"));
        QVERIFY(scripture.showNow());
        QCOMPARE(liveStyle().value("valign").toString(), QStringLiteral("bottom"));
        QVERIFY(ui.show.scene().layer(Layer::Slide).content.text.startsWith(QStringLiteral("16 For God so loved")));

        // Side by side with Yoruba: two columns, each with its own reference
        // directly under it (in its own language), and the credits below.
        QVERIFY(ui.click("referencePosition-below"));
        QVERIFY(ui.click("versesPerSlide-1"));
        QVERIFY(ui.click("second-yor"));
        QVERIFY(scripture.secondTranslation() >= 0);
        QVERIFY(scripture.previewSecondText().startsWith(m_expected["yor"].start));
        QTRY_VERIFY(ui.item("scripturePreviewSecond")->isVisible());
        QVERIFY(scripture.showNow());
        const Content live = ui.show.scene().layer(Layer::Slide).content;
        QCOMPARE(live.kind, Content::Kind::Composed);
        const auto text = [](const Content &c, int i) { return c.elements[i].toObject().value("text").toString(); };
        const auto frameOf = [](const Content &c, int i) { return c.elements[i].toObject().value("frame").toArray(); };
        QCOMPARE(live.elements.size(), 5);
        QVERIFY(text(live, 0).startsWith(m_expected["engwebp"].start));
        QVERIFY(text(live, 1).startsWith(m_expected["yor"].start));
        QCOMPARE(text(live, 2), QStringLiteral("John 3:16 (WEB)"));
        QCOMPARE(text(live, 3), QStringLiteral("Johanu 3:16 (Yorùbá)"));
        for (int column : {0, 1}) {
            const QJsonArray words = frameOf(live, column);
            const QJsonArray reference = frameOf(live, column + 2);
            QCOMPARE(reference[0].toDouble(), words[0].toDouble()); // the same column
            const double gap = reference[1].toDouble() - (words[1].toDouble() + words[3].toDouble());
            QVERIFY2(gap >= 0 && gap < 20, qPrintable(QString::number(gap))); // directly under
        }
        QCOMPARE(live.elements[0].toObject().value("style").toObject().value("valign").toString(), QStringLiteral("bottom"));
        const QString credit = text(live, 4);
        QVERIFY2(credit.contains(QStringLiteral("World English Bible")) && credit.contains(QStringLiteral("Biblica")), qPrintable(credit));

        // Copyright line hidden: the public-domain WEB's goes; the Yoruba
        // Bible keeps a short credit (CC BY-SA asks for one), on the first
        // slide of the passage only.
        QVERIFY(ui.click("copyrightOff"));
        QVERIFY(!scripture.showCopyright());
        scripture.lookup(QStringLiteral("jn 3 16-17"));
        QVERIFY(scripture.showNow());
        const Content hidden = ui.show.scene().layer(Layer::Slide).content;
        QCOMPARE(hidden.elements.size(), 5);
        QCOMPARE(text(hidden, 4), QStringLiteral("Yorùbá © Biblica, CC BY-SA 4.0"));
        QVERIFY(ui.controller->goLive(1));
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content.elements.size(), 4); // no credit

        // Added to the plan, it comes back the same.
        QVERIFY(scripture.addToPlan());
        const int last = ui.controller->plan()->property("count").toInt() - 1;
        ui.controller->selectItem(last);
        QVERIFY(ui.controller->itemTitle().contains(QStringLiteral("+")));
        QVERIFY(ui.controller->goLive(1));
        const Content fromPlan = ui.show.scene().layer(Layer::Slide).content;
        QCOMPARE(fromPlan.kind, Content::Kind::Composed);
        QCOMPARE(fromPlan.elements.size(), 4); // the credit is on slide 1
        QCOMPARE(text(fromPlan, 3), QStringLiteral("Johanu 3:17 (Yorùbá)"));
        QVERIFY(ui.controller->goLive(0));
        QCOMPARE(text(ui.show.scene().layer(Layer::Slide).content, 4), QStringLiteral("Yorùbá © Biblica, CC BY-SA 4.0"));

        // One public-domain translation with the line hidden: just the
        // reference. Shown again: the copyright is back.
        QVERIFY(ui.click("second-off"));
        QCOMPARE(scripture.secondTranslation(), -1);
        QVERIFY(scripture.showNow());
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content.footer, QStringLiteral("John 3:16 (WEB)"));
        // Yoruba alone, hidden: the short credit on the first slide only,
        // also after a trip through the plan.
        QVERIFY(ui.click("translation-yor"));
        QVERIFY(scripture.showNow());
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content.footer,
                 QStringLiteral("Johanu 3:16 (Yorùbá) · Yorùbá © Biblica, CC BY-SA 4.0"));
        QVERIFY(ui.controller->goLive(1));
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content.footer, QStringLiteral("Johanu 3:17 (Yorùbá)"));
        QVERIFY(scripture.addToPlan());
        ui.controller->selectItem(ui.controller->plan()->property("count").toInt() - 1);
        QVERIFY(ui.controller->goLive(0));
        QVERIFY(ui.show.scene().layer(Layer::Slide).content.footer.endsWith(QStringLiteral("Yorùbá © Biblica, CC BY-SA 4.0")));
        QVERIFY(ui.controller->goLive(1));
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content.footer, QStringLiteral("Johanu 3:17 (Yorùbá)"));
        QVERIFY(ui.click("translation-engwebp"));
        QVERIFY(ui.click("copyrightOn"));
        QVERIFY(scripture.showNow());
        QCOMPARE(ui.show.scene().layer(Layer::Slide).content.footer,
                 QStringLiteral("John 3:16 (WEB) · World English Bible. Public domain."));

        // The reference above, added and reloaded: still above.
        QVERIFY(ui.click("referencePosition-above"));
        QVERIFY(scripture.addToPlan());
        ui.controller->selectItem(ui.controller->plan()->property("count").toInt() - 1);
        QVERIFY(ui.controller->goLive(0));
        QCOMPARE(liveStyle().value("footerPosition").toString(), QStringLiteral("above"));
    }

    // A church brings its own Bible: OpenLP CSV (books.csv and verses.csv),
    // picked by its verses.csv, named after its folder, looked up like any.
    void addOwnBible()
    {
        OperatorHarness ui;
        ui.bibles = m_store.get();
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QTemporaryDir dir;
        auto user = BibleStore::open(dir.filePath(QStringLiteral("user-bibles.db")), false);
        QVERIFY(user.ok());
        ui.scripture->setStores(m_store.get(), user.value().get());
        const int before = int(ui.scripture->translations().size());
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("YorubaBible1900")));
        QFile books(dir.filePath(QStringLiteral("YorubaBible1900/books.csv")));
        QFile verses(dir.filePath(QStringLiteral("YorubaBible1900/verses.csv")));
        QVERIFY(books.open(QIODevice::WriteOnly) && verses.open(QIODevice::WriteOnly));
        books.write("43,2,\"Johanu\",\"Joh\"\n");
        verses.write("Johanu,3,16,\" 16 Nitori Ọlọrun fẹ araiye tobẹ̃ gẹ\"\nJohanu,3,17,\"Nitori Ọlọrun kò rán Ọmọ rẹ̀\"\n");
        books.close();
        verses.close();
        const QString summary = ui.scripture->addBible(QUrl::fromLocalFile(dir.filePath(QStringLiteral("YorubaBible1900/verses.csv"))));
        QVERIFY2(summary.startsWith(QStringLiteral("Added YorubaBible1900")), qPrintable(summary));
        QCOMPARE(int(ui.scripture->translations().size()), before + 1);
        int added = -1;
        for (int i = 0; i < ui.scripture->translations().size(); ++i) {
            if (ui.scripture->translations().at(i).toMap().value("id").toString() == QStringLiteral("user-yorubabible1900"))
                added = i;
        }
        QVERIFY(added >= 0);
        ui.scripture->selectTranslation(added);
        ui.scripture->lookup(QStringLiteral("johanu 3 16"));
        QCOMPARE(ui.scripture->label(), QStringLiteral("Johanu 3:16"));
        QCOMPARE(ui.scripture->previewText(), QStringLiteral("Nitori Ọlọrun fẹ araiye tobẹ̃ gẹ"));
        // Its notice says to check the licence before public use.
        QVERIFY(ui.scripture->copyright().contains(QStringLiteral("check its licence")));
        ui.scripture->setStores(m_store.get(), nullptr);
    }

    void referencePartsInTheWindow()
    {
        OperatorHarness ui;
        ui.bibles = m_store.get();
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QTest::keyClick(ui.window, Qt::Key_B, Qt::ControlModifier);
        QTRY_VERIFY(ui.item("scriptureDialog"));
        QQuickWindow *window = ui.item("scriptureDialog")->window();
        QVERIFY(ui.click("translation-engwebp"));
        QQuickItem *book = ui.item("scriptureReference");
        QQuickItem *chapter = ui.item("scriptureChapter");
        QQuickItem *verse = ui.item("scriptureVerse");
        book->forceActiveFocus();
        QTRY_VERIFY(book->hasActiveFocus());
        const auto text = [](QQuickItem *part) { return part->property("text").toString(); };
        const auto selected = [](QQuickItem *part) { return part->property("selectedText").toString(); };

        QTest::keyClick(window, 'j');
        QCOMPARE(text(book), QStringLiteral("John"));
        QCOMPARE(selected(book), QStringLiteral("ohn"));
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(chapter->hasActiveFocus());
        QTest::keyClick(window, '3');
        QCOMPARE(ui.scripture->label(), QStringLiteral("John 3"));
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(verse->hasActiveFocus());
        QTest::keyClick(window, '1');
        QTest::keyClick(window, '6');
        QCOMPARE(ui.scripture->label(), QStringLiteral("John 3:16"));
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY(ui.controller->previewText().startsWith(QStringLiteral("For God so loved")));

        // Back with Left: the chapter, then the book, each highlighted.
        QTest::keyClick(window, Qt::Key_Left);
        QVERIFY(chapter->hasActiveFocus());
        QCOMPARE(selected(chapter), QStringLiteral("3"));
        QTest::keyClick(window, Qt::Key_Left);
        QVERIFY(book->hasActiveFocus());
        QCOMPARE(selected(book), QStringLiteral("John"));
        // A new book over it: "ro" is Romans; the chapter and verse stay to
        // be typed over.
        QTest::keyClick(window, 'r');
        QTest::keyClick(window, 'o');
        QCOMPARE(text(book), QStringLiteral("Romans"));
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(selected(chapter), QStringLiteral("3"));
        QTest::keyClick(window, '8');
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(selected(verse), QStringLiteral("16"));
        QTest::keyClick(window, '2');
        QTest::keyClick(window, '8');
        QCOMPARE(ui.scripture->label(), QStringLiteral("Romans 8:28"));
        // Shift+Enter: on the screen now.
        QTest::keyClick(window, Qt::Key_Return, Qt::ShiftModifier);
        QVERIFY(ui.controller->liveText().startsWith(QStringLiteral("We know that all things")));
        // Up: straight to the book.
        QTest::keyClick(window, Qt::Key_Up);
        QVERIFY(book->hasActiveFocus());
        QCOMPARE(selected(book), QStringLiteral("Romans"));

        // Backspace takes back the filled-in rest and keeps the letter typed
        // ("J", not John again).
        QTest::keyClick(window, 'j');
        QTest::keyClick(window, Qt::Key_Backspace);
        QCOMPARE(text(book), QStringLiteral("J"));
        // "j" and Enter: John 1:1.
        verse->setProperty("text", QString());
        chapter->setProperty("text", QString());
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(ui.scripture->label(), QStringLiteral("John 1:1"));
        QVERIFY(ui.controller->previewText().startsWith(QStringLiteral("In the beginning")));

        // The safety keys work from here too: F7 is Black.
        QTest::keyClick(window, Qt::Key_F7);
        QVERIFY(ui.show.scene().black);
        QTest::keyClick(window, Qt::Key_F7);
        QVERIFY(!ui.show.scene().black);
    }
};

QTEST_MAIN(TestScriptureBundled)
#include "tst_scripture_bundled.moc"
