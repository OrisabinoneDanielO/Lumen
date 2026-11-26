#include "keymap.h"
#include "operatorcontroller.h"
#include "sampleservice.h"
#include "servicedata.h"

#include "lumen/core/data/repository.h"
#include "lumen/core/songs/songlibrary.h"

#include <QSignalSpy>
#include <QTest>

using namespace lumen;
using namespace lumen::core;
using namespace lumen::core::data;
using namespace lumen::core::show;
using lumen::app::KeyAction;

namespace {

// One song with every kind of section, arranged
//   V1 PC C V2 PC C B C E   (Chorus has two slides, the rest one)
// plus a plan: a header, the song, and the song again with a short
// arrangement (V1 C).
struct Fixture
{
    std::unique_ptr<Database> db;
    QString planId;

    template <typename T>
    T add(T entity)
    {
        auto created = Repository<T>(*db).create(std::move(entity));
        if (!created.ok())
            qFatal("fixture: %s", qPrintable(created.error().message));
        return created.value();
    }

    Group group(const QString &presentationId, int position, const QString &label,
                const QStringList &texts)
    {
        const Group g = add(Group{.presentationId = presentationId, .position = position, .label = label});
        int slidePosition = 0;
        for (const QString &text : texts) {
            const Slide s = add(Slide{.groupId = g.id, .position = slidePosition++});
            add(Element{.slideId = s.id, .type = ElementType::Text, .content = text});
        }
        return g;
    }

    Fixture()
    {
        db = std::move(Database::open(Database::inMemory())).value();
        const Library library = add(Library{.name = "Test"});
        const Presentation song = add(Presentation{.libraryId = library.id,
                                                   .title = "Test Song",
                                                   .metadata = {{"author", "A. Writer"}}});
        const Group v1 = group(song.id, 0, "Verse 1", {"v1"});
        const Group pc = group(song.id, 1, "Pre-chorus", {"pc"});
        const Group c = group(song.id, 2, "Chorus", {"c-a", "c-b"});
        const Group v2 = group(song.id, 3, "Verse 2", {"v2"});
        const Group b = group(song.id, 4, "Bridge", {"b"});
        const Group e = group(song.id, 5, "Ending", {"e"});
        add(Arrangement{.presentationId = song.id, .name = "Full", .isDefault = true,
                        .groupIds = {v1.id, pc.id, c.id, v2.id, pc.id, c.id, b.id, c.id, e.id}});
        const Arrangement shortOne = add(Arrangement{.presentationId = song.id, .name = "Short",
                                                     .groupIds = {v1.id, c.id}});
        const Plan plan = add(Plan{.name = "Test plan", .date = QDate(2026, 11, 29)});
        planId = plan.id;
        add(PlanItem{.planId = plan.id, .position = 0, .headerText = "Worship"});
        add(PlanItem{.planId = plan.id, .position = 1, .presentationId = song.id});
        add(PlanItem{.planId = plan.id, .position = 2, .presentationId = song.id, .arrangementId = shortOne.id});
    }
};

// Slide texts of the full arrangement, by index.
const QStringList fullOrder{"v1", "pc", "c-a", "c-b", "v2", "pc", "c-a", "c-b", "b", "c-a", "c-b", "e"};

} // namespace

class TestOperator : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Fixture> m_fixture;
    std::unique_ptr<ShowController> m_show;
    std::unique_ptr<app::OperatorController> m_op;

    QString onAir() const
    {
        const Content &content = m_show->scene().layer(Layer::Slide).content;
        return content.kind == Content::Kind::Text ? content.text : QString();
    }

    bool key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        return m_op->handleKey(key, int(modifiers));
    }

private slots:
    void init()
    {
        m_fixture = std::make_unique<Fixture>();
        m_show = std::make_unique<ShowController>();
        m_op = std::make_unique<app::OperatorController>(*m_show, true);
        auto plan = app::loadPlan(*m_fixture->db, m_fixture->planId);
        QVERIFY(plan.ok());
        m_op->setPlan(plan.value());
        m_op->selectItem(1); // the song, full arrangement
    }

    void cleanup()
    {
        m_op.reset();
        m_show.reset();
        m_fixture.reset();
    }

    // ---- loading the plan ---------------------------------------------

    void loaderExpandsTheArrangement()
    {
        auto plan = app::loadPlan(*m_fixture->db, m_fixture->planId);
        QVERIFY(plan.ok());
        const auto &items = plan.value().items;
        QCOMPARE(items.size(), std::size_t(3));

        QCOMPARE(items[0].kind, QString()); // header
        QCOMPARE(items[0].title, QStringLiteral("Worship"));
        QVERIFY(items[0].slides.empty());

        QStringList texts;
        for (const auto &slide : items[1].slides)
            texts << slide.text;
        QCOMPARE(texts, fullOrder);
        QCOMPARE(items[1].kind, QStringLiteral("SONG"));
        QCOMPARE(items[1].subtitle, QStringLiteral("A. Writer"));
        QStringList chips;
        for (const auto &chip : items[1].chips)
            chips << chip.label;
        QCOMPARE(chips, (QStringList{"V1", "PC", "C", "V2", "PC", "C", "B", "C", "E"}));

        // The plan item's own arrangement wins over the default.
        QCOMPARE(items[2].slides.size(), std::size_t(3));
        QCOMPARE(items[2].slides[2].text, QStringLiteral("c-b"));
    }

    void groupColoursAndAbbreviations()
    {
        QCOMPARE(app::defaultGroupColour("Verse 3"), QColor(0x5B, 0x8D, 0xEF));
        QCOMPARE(app::defaultGroupColour("Chorus"), QColor(0xE5, 0x48, 0x4D));
        QCOMPARE(app::defaultGroupColour("Bridge"), QColor(0xA7, 0x79, 0xE9));
        QCOMPARE(app::groupAbbreviation("Verse 12"), QStringLiteral("V12"));
        QCOMPARE(app::groupAbbreviation("Chorus 2"), QStringLiteral("C2"));
        QCOMPARE(app::groupAbbreviation("Pre-chorus"), QStringLiteral("PC"));
        QCOMPARE(app::groupAbbreviation("Tag"), QStringLiteral("Tag"));
    }

    void sampleServiceLoads()
    {
        auto db = std::move(Database::open(Database::inMemory())).value();
        QVERIFY(app::seedSampleService(*db).ok());
        auto plan = app::loadLatestPlan(*db);
        QVERIFY(plan.ok() && plan.value());
        QCOMPARE(plan.value()->items.size(), std::size_t(9));
        QCOMPARE(plan.value()->items[0].kind, QStringLiteral("TIMER"));
        QCOMPARE(plan.value()->items[4].kind, QStringLiteral("BIBLE"));
        QCOMPARE(plan.value()->items[6].kind, QStringLiteral("MEDIA"));
        auto songs = app::loadSongs(*db);
        QVERIFY(songs.ok());
        QCOMPARE(songs.value().size(), std::size_t(4));
        QCOMPARE(songs.value().front().title, QStringLiteral("Amazing Grace"));
    }

    void noPlanMeansEmpty()
    {
        auto db = std::move(Database::open(Database::inMemory())).value();
        auto plan = app::loadLatestPlan(*db);
        QVERIFY(plan.ok());
        QVERIFY(!plan.value());
    }

    // ---- key map -------------------------------------------------------

    void keyMapDefaults()
    {
        const app::KeyMap keys;
        auto action = [&keys](int key, Qt::KeyboardModifiers m = Qt::NoModifier) {
            const auto binding = keys.lookup(key, m);
            return binding ? int(binding->action) : -1;
        };
        QCOMPARE(action(Qt::Key_Space), int(KeyAction::NextSlide));
        QCOMPARE(action(Qt::Key_Right), int(KeyAction::NextSlide));
        QCOMPARE(action(Qt::Key_Left), int(KeyAction::PreviousSlide));
        QCOMPARE(action(Qt::Key_PageDown), int(KeyAction::NextItem));
        QCOMPARE(action(Qt::Key_PageUp), int(KeyAction::PreviousItem));
        QCOMPARE(action(Qt::Key_F1), int(KeyAction::ClearBackground));
        QCOMPARE(action(Qt::Key_F2), int(KeyAction::ClearSlide));
        QCOMPARE(action(Qt::Key_F3), int(KeyAction::ClearProps));
        QCOMPARE(action(Qt::Key_F4), int(KeyAction::ClearMessages));
        QCOMPARE(action(Qt::Key_F5), int(KeyAction::ClearAll));
        QCOMPARE(action(Qt::Key_F6), int(KeyAction::Logo));
        QCOMPARE(action(Qt::Key_F7), int(KeyAction::Black));
        QCOMPARE(keys.lookup(Qt::Key_C, {})->group, QStringLiteral("Chorus"));
        QCOMPARE(keys.lookup(Qt::Key_P, {})->group, QStringLiteral("Pre-chorus"));
        QCOMPARE(keys.lookup(Qt::Key_7, {})->section, 7);
        QCOMPARE(keys.lookup(Qt::Key_7, Qt::KeypadModifier)->section, 7);
        // Modified keys are not live shortcuts.
        QCOMPARE(action(Qt::Key_Space, Qt::ShiftModifier), -1);
        QCOMPARE(action(Qt::Key_C, Qt::ControlModifier), -1);
        QCOMPARE(action(Qt::Key_B, Qt::ControlModifier), int(KeyAction::OpenScripture)); // batch 7
        QCOMPARE(action(Qt::Key_Q), -1);
    }

    // ---- going live ----------------------------------------------------

    void selectingAnItemSendsNothing()
    {
        QCOMPARE(m_show->scene().sequence, quint64(0));
        QCOMPARE(m_op->liveSlide(), -1);
        QCOMPARE(m_op->previewSlide(), 0);
        QCOMPARE(m_op->previewText(), QStringLiteral("v1"));
    }

    void goLiveAndPreview()
    {
        QVERIFY(m_op->goLive(4));
        QCOMPARE(onAir(), QStringLiteral("v2"));
        QCOMPARE(m_op->liveItem(), 1);
        QCOMPARE(m_op->liveSlide(), 4);
        QCOMPARE(m_op->previewSlide(), 5);
        QCOMPARE(m_op->liveLabel(), QStringLiteral("Verse 2 · slide 5"));
        QVERIFY(!m_op->goLive(99));
        QVERIFY(!m_op->goLive(-1));
        QCOMPARE(onAir(), QStringLiteral("v2"));
    }

    void spaceStepsThroughAndStopsAtTheEnd()
    {
        for (int i = 0; i < fullOrder.size(); ++i) {
            QVERIFY(key(Qt::Key_Space));
            QCOMPARE(onAir(), fullOrder[i]);
        }
        QCOMPARE(m_op->previewSlide(), -1);
        const quint64 sequence = m_show->scene().sequence;
        QVERIFY(key(Qt::Key_Space)); // handled, but stays on the last slide
        QCOMPARE(onAir(), QStringLiteral("e"));
        QCOMPARE(m_show->scene().sequence, sequence);
        QCOMPARE(m_op->currentItem(), 1);
    }

    void leftGoesBackButNotPastTheStart()
    {
        m_op->goLive(2);
        QVERIFY(key(Qt::Key_Left));
        QCOMPARE(onAir(), QStringLiteral("pc"));
        key(Qt::Key_Left);
        QCOMPARE(onAir(), QStringLiteral("v1"));
        key(Qt::Key_Left);
        QCOMPARE(onAir(), QStringLiteral("v1"));
    }

    // Page Down shows the next item; Space then sends its first slide.
    void pageDownSelectsTheNextItemWithoutGoingLive()
    {
        m_op->goLive(3);
        QVERIFY(key(Qt::Key_PageDown));
        QCOMPARE(m_op->currentItem(), 2);
        QCOMPARE(onAir(), QStringLiteral("c-b"));    // still the old slide
        QCOMPARE(m_op->liveSlide(), -1);             // not from this item
        QCOMPARE(m_op->previewSlide(), 0);
        key(Qt::Key_Space);
        QCOMPARE(onAir(), QStringLiteral("v1"));
        QCOMPARE(m_op->liveItem(), 2);
        key(Qt::Key_PageDown); // already the last item
        QCOMPARE(m_op->currentItem(), 2);
        key(Qt::Key_PageUp);
        key(Qt::Key_PageUp);
        QCOMPARE(m_op->currentItem(), 0);
        key(Qt::Key_PageUp);
        QCOMPARE(m_op->currentItem(), 0);
    }

    void letterKeysJumpToSections()
    {
        QVERIFY(key(Qt::Key_C));
        QCOMPARE(m_op->liveSlide(), 2); // first chorus
        key(Qt::Key_C);
        QCOMPARE(m_op->liveSlide(), 6); // next chorus
        key(Qt::Key_C);
        QCOMPARE(m_op->liveSlide(), 9);
        key(Qt::Key_C);
        QCOMPARE(m_op->liveSlide(), 2); // wraps round
        key(Qt::Key_B);
        QCOMPARE(onAir(), QStringLiteral("b"));
        key(Qt::Key_P);
        QCOMPARE(m_op->liveSlide(), 1); // after the bridge there is none; wraps
        key(Qt::Key_E);
        QCOMPARE(onAir(), QStringLiteral("e"));
    }

    void verseAndSectionNumbers()
    {
        key(Qt::Key_V);
        key(Qt::Key_2);
        QCOMPARE(onAir(), QStringLiteral("v2"));
        // Without V a number is the section in play order: 3 = first chorus.
        key(Qt::Key_3);
        QCOMPARE(m_op->liveSlide(), 2);
        key(Qt::Key_9);
        QCOMPARE(onAir(), QStringLiteral("e"));
        key(Qt::Key_V);
        key(Qt::Key_7); // there is no verse 7: nothing changes
        QCOMPARE(onAir(), QStringLiteral("e"));
    }

    void clearKeysLogoAndBlack()
    {
        m_show->setContent(Layer::Background, Content::fill(Qt::blue));
        m_show->setContent(Layer::Props, Content::textContent("Name"));
        m_show->setContent(Layer::Messages, Content::textContent("Message"));
        m_op->goLive(0);

        key(Qt::Key_F3);
        QVERIFY(m_show->scene().layer(Layer::Props).content.isEmpty());
        key(Qt::Key_F4);
        QVERIFY(m_show->scene().layer(Layer::Messages).content.isEmpty());
        key(Qt::Key_F1);
        QVERIFY(m_show->scene().layer(Layer::Background).content.isEmpty());
        key(Qt::Key_F2);
        QVERIFY(m_show->scene().layer(Layer::Slide).content.isEmpty());
        QCOMPARE(m_op->liveSlide(), -1);
        QCOMPARE(m_op->liveLabel(), QStringLiteral("Clear"));
        // After Clear Slide, Space carries on from where the song was.
        QCOMPARE(m_op->previewSlide(), 1);

        key(Qt::Key_F6);
        QVERIFY(m_op->logoOn());
        key(Qt::Key_F7);
        QVERIFY(m_op->blackOn());
        key(Qt::Key_F7);
        QVERIFY(!m_op->blackOn());

        m_op->goLive(2);
        key(Qt::Key_F5);
        for (const LayerState &layer : m_show->scene().layers)
            QVERIFY(layer.content.isEmpty());
        QVERIFY(m_op->logoOn()); // Clear All leaves Logo and Black alone
    }

    void layersPanelDescribesTheScene()
    {
        m_show->setContent(Layer::Background, Content::fill(QColor(0x14, 0x21, 0x3D)));
        m_op->goLive(4);
        const QVariantList layers = m_op->layers();
        QCOMPARE(layers.size(), 5);
        auto row = [&layers](int i) { return layers[i].toMap(); };
        QCOMPARE(row(0).value("name").toString(), QStringLiteral("Messages"));
        QCOMPARE(row(0).value("value").toString(), QStringLiteral("Off"));
        QCOMPARE(row(2).value("value").toString(), QStringLiteral("Verse 2 · slide 5"));
        QCOMPARE(row(4).value("value").toString(), QStringLiteral("Colour #14213D"));
        QVERIFY(row(4).value("active").toBool());
    }

    // Edit mode: nothing but the safety controls may touch the screen.
    void editModeNeverGoesLive()
    {
        m_op->goLive(0);
        const quint64 sequence = m_show->scene().sequence;
        m_op->setMode("edit");
        QCOMPARE(m_op->mode(), QStringLiteral("edit"));

        QVERIFY(!m_op->goLive(3));
        QVERIFY(!m_op->next());
        m_op->activateSlide(5); // selects for editing instead
        QCOMPARE(m_op->selectedSlide(), 5);
        QVERIFY(!key(Qt::Key_Space));
        QVERIFY(!key(Qt::Key_C));
        QVERIFY(!key(Qt::Key_3));
        QCOMPARE(m_show->scene().sequence, sequence);
        QCOMPARE(onAir(), QStringLiteral("v1"));

        QVERIFY(key(Qt::Key_F7)); // Black still works
        QVERIFY(m_op->blackOn());

        m_op->setMode("show");
        m_op->activateSlide(5);
        QCOMPARE(onAir(), QStringLiteral("pc"));
    }

    void editModeNeedsTheSetting()
    {
        ShowController show;
        app::OperatorController locked(show, false);
        locked.setMode("edit");
        QCOMPARE(locked.mode(), QStringLiteral("show"));
        QVERIFY(!locked.editAvailable());
    }

    void headerItemsHaveNoSlides()
    {
        m_op->selectItem(0);
        QCOMPARE(m_op->itemTitle(), QStringLiteral("Worship"));
        QCOMPARE(m_op->previewSlide(), -1);
        QVERIFY(!m_op->next());
        QCOMPARE(m_show->scene().sequence, quint64(0));
    }

    // Regression (batch 6): browsing to another library song while one is
    // live must not mix up the two songs' slides (it used to crash).
    void browsingLibrarySongsWhileOneIsLive()
    {
        songs::SongLibrary library(*m_fixture->db);
        auto shortSong = songs::parseTextSong(QStringLiteral("Title: Short\n\nOnly line\n"), {});
        QVERIFY(library.add(shortSong.value()).ok());
        QVERIFY(library.indexMissing().ok());
        m_op->attachLibrary(*m_fixture->db, library);

        auto rowOf = [this](const QString &title) {
            auto *songsModel = static_cast<app::RowModel *>(m_op->songs());
            for (int i = 0; i < songsModel->count(); ++i) {
                if (songsModel->get(i).value("title").toString() == title)
                    return i;
            }
            return -1;
        };
        QVERIFY(m_op->showSong(rowOf("Test Song")));
        QVERIFY(m_op->goLive(11)); // the last slide of 12
        QCOMPARE(m_op->liveLabel(), QStringLiteral("Ending · slide 12"));

        QVERIFY(m_op->showSong(rowOf("Short"))); // one slide only
        QCOMPARE(m_op->liveLabel(), QStringLiteral("Ending · slide 12")); // still what is on air
        QCOMPARE(m_op->liveSlide(), -1);  // not a slide of this song
        QCOMPARE(m_op->previewSlide(), 0);
        QCOMPARE(m_op->layers().size(), 5); // reading the panel used to crash
        QVERIFY(!m_op->previous());

        QVERIFY(m_op->showSong(rowOf("Test Song")));
        QCOMPARE(m_op->liveSlide(), 11); // back on the live song
        QVERIFY(m_op->previous());
        QCOMPARE(onAir(), QStringLiteral("c-b"));

        QVERIFY(m_op->showSong(rowOf("Short")));
        QVERIFY(m_op->next());
        QCOMPARE(onAir(), QStringLiteral("Only line"));
        QCOMPARE(m_op->liveLabel(), QStringLiteral("Verse 1 · slide 1"));
    }

    void planDetail()
    {
        QCOMPARE(m_op->planName(), QStringLiteral("Test plan"));
        QVERIFY2(m_op->planDetail().endsWith(QStringLiteral(" · 3 items")), qPrintable(m_op->planDetail()));
        m_op->setPlan(std::nullopt);
        QCOMPARE(m_op->planName(), QStringLiteral("No service plan"));
        QCOMPARE(m_op->currentItem(), -1);
    }
};

QTEST_GUILESS_MAIN(TestOperator)
#include "tst_operator.moc"
