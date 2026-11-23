#include "lumen/core/data/database.h"
#include "lumen/core/data/repository.h"

#include <QCoreApplication>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <algorithm>
#include <memory>

using namespace lumen::core;
using namespace lumen::core::data;

namespace {

template <typename T>
QByteArray why(const Result<T> &result)
{
    return result.ok() ? QByteArray() : result.error().message.toUtf8();
}

template <typename T>
bool containsId(const std::vector<T> &rows, const QString &id)
{
    return std::any_of(rows.begin(), rows.end(), [&](const T &row) { return row.id == id; });
}

template <typename T>
QStringList ids(const std::vector<T> &rows)
{
    QStringList out;
    for (const T &row : rows)
        out << row.id;
    return out;
}

// Text in Yoruba, Igbo and Hausa, with the tone marks and hooked letters the
// design doc calls out.
const QString yoruba = QStringLiteral("Ẹ jẹ́ ká yin Olúwa, Ọba ògo");
const QString igbo = QStringLiteral("Ọ dị mma, Chineke m, ị dị ukwuu");
const QString hausa = QStringLiteral("Ɗaukaka ga Allah, ƙauna ta ƴanci");

} // namespace

// Creates, reads, updates and deletes every entity in the Data model, and
// checks ordering and delete rules. Runs once on a file database and once in
// memory (see main below).
class TestRepositories : public QObject
{
    Q_OBJECT

public:
    explicit TestRepositories(bool inMemory) : m_inMemory(inMemory) {}

private:
    // Create -> get -> update -> get -> list -> remove -> get (NotFound).
    template <typename T, typename Change>
    void checkCrud(T entity, Change change)
    {
        Repository<T> repo(*m_db);

        auto created = repo.create(entity);
        QVERIFY2(created.ok(), why(created));
        const T stored = created.value();
        QVERIFY(!stored.id.isEmpty());

        auto fetched = repo.get(stored.id);
        QVERIFY2(fetched.ok(), why(fetched));
        QVERIFY(fetched.value() == stored);

        T changed = stored;
        change(changed);
        QVERIFY2(!(changed == stored), "the change must alter the entity");
        auto updated = repo.update(changed);
        QVERIFY2(updated.ok(), why(updated));
        auto refetched = repo.get(stored.id);
        QVERIFY2(refetched.ok(), why(refetched));
        QVERIFY(refetched.value() == changed);

        auto all = repo.list();
        QVERIFY2(all.ok(), why(all));
        QVERIFY(containsId(all.value(), stored.id));

        auto removed = repo.remove(stored.id);
        QVERIFY2(removed.ok(), why(removed));
        auto gone = repo.get(stored.id);
        QVERIFY(!gone.ok());
        QCOMPARE(gone.error().code, Error::Code::NotFound);

        auto removedAgain = repo.remove(stored.id);
        QVERIFY(!removedAgain.ok());
        QCOMPARE(removedAgain.error().code, Error::Code::NotFound);
        auto updateMissing = repo.update(changed);
        QVERIFY(!updateMissing.ok());
        QCOMPARE(updateMissing.error().code, Error::Code::NotFound);
    }

    template <typename T>
    T make(T entity)
    {
        auto created = Repository<T>(*m_db).create(entity);
        if (!created.ok())
            qFatal("fixture: %s", qPrintable(created.error().message));
        return created.value();
    }

    template <typename T>
    bool exists(const QString &id)
    {
        return Repository<T>(*m_db).get(id).ok();
    }

    bool m_inMemory;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Database> m_db;

    // Parents most tests need.
    Library m_library;
    Theme m_theme;
    Presentation m_song;
    Group m_verse;
    Slide m_slide;
    MediaAsset m_video;
    Plan m_plan;
    StageLayout m_stage;

private slots:
    void initTestCase()
    {
        qInfo("Database: %s", m_inMemory ? "in memory" : "file");
    }

    void init()
    {
        QString path = Database::inMemory();
        if (!m_inMemory) {
            m_dir = std::make_unique<QTemporaryDir>();
            QVERIFY(m_dir->isValid());
            path = m_dir->filePath(QStringLiteral("library.db"));
        }
        auto db = Database::open(path);
        QVERIFY2(db.ok(), why(db));
        m_db = std::move(db).value();

        m_library = make<Library>({.name = "Main library", .folderPath = "/srv/lumen"});
        m_theme = make<Theme>({.name = "Lyrics, centred", .appliesTo = ThemeTarget::Song});
        m_song = make<Presentation>({.libraryId = m_library.id,
                                     .kind = PresentationKind::Song,
                                     .title = "Amazing Grace",
                                     .themeId = m_theme.id});
        m_verse = make<Group>({.presentationId = m_song.id, .label = "Verse 1"});
        m_slide = make<Slide>({.groupId = m_verse.id});
        m_video = make<MediaAsset>({.libraryId = m_library.id, .filePath = "/media/loop.mp4"});
        m_plan = make<Plan>({.name = "Sunday Service", .date = QDate(2026, 11, 29)});
        m_stage = make<StageLayout>({.name = "Band"});
    }

    void cleanup()
    {
        m_db.reset();
        m_dir.reset();
    }

    // ---- create, read, update and delete every entity ------------------

    void crudLibrary()
    {
        checkCrud(Library{.name = "Youth library",
                          .folderPath = "/home/media/youth",
                          .lastBackup = QDateTime(QDate(2026, 11, 22), QTime(9, 30, 15, 250),
                                                  QTimeZone::UTC)},
                  [](Library &e) {
                      e.name = yoruba;
                      e.lastBackup.reset();
                  });
    }

    void crudPresentation()
    {
        checkCrud(Presentation{.libraryId = m_library.id,
                               .kind = PresentationKind::Song,
                               .title = "Be Thou My Vision",
                               .themeId = m_theme.id,
                               .metadata = {{"author", "Trad. Irish"}, {"key", "Eb"}, {"tempo", 72}}},
                  [](Presentation &e) {
                      e.kind = PresentationKind::Scripture;
                      e.title = igbo;
                      e.themeId.clear();
                      e.metadata[QStringLiteral("copyright")] = QStringLiteral("Public domain");
                  });
    }

    void crudGroup()
    {
        checkCrud(Group{.presentationId = m_song.id,
                        .position = 1,
                        .label = "Chorus",
                        .colour = "#E5484D",
                        .hotkey = "C"},
                  [](Group &e) {
                      e.position = 4;
                      e.label = "Bridge";
                      e.colour = "#A779E9";
                      e.hotkey = "B";
                  });
    }

    void crudArrangement()
    {
        const Group chorus = make<Group>({.presentationId = m_song.id, .position = 1, .label = "Chorus"});
        const Group bridge = make<Group>({.presentationId = m_song.id, .position = 2, .label = "Bridge"});
        checkCrud(Arrangement{.presentationId = m_song.id,
                              .name = "Sunday",
                              .isDefault = true,
                              .groupIds = {m_verse.id, chorus.id, m_verse.id, chorus.id}},
                  [&](Arrangement &e) {
                      e.name = "Long";
                      e.isDefault = false;
                      e.groupIds = {m_verse.id, chorus.id, bridge.id, chorus.id, chorus.id};
                  });
    }

    void crudSlide()
    {
        checkCrud(Slide{.groupId = m_verse.id,
                        .position = 1,
                        .notes = "Band drops out",
                        .chords = "G C G D",
                        .transition = {{"type", "fade"}, {"durationMs", 500}}},
                  [](Slide &e) {
                      e.position = 0;
                      e.notes = hausa;
                      e.transition = {};
                  });
    }

    void crudElement()
    {
        checkCrud(Element{.slideId = m_slide.id,
                          .type = ElementType::Text,
                          .frame = QRectF(96, 540, 1728, 400),
                          .style = {{"font", "Inter"}, {"size", 64}, {"outline", true}},
                          .zOrder = 2,
                          .content = "Amazing grace! How sweet the sound"},
                  [&](Element &e) {
                      e.type = ElementType::Video;
                      e.frame = QRectF(0, 0, 1920, 1080);
                      e.zOrder = 0;
                      e.content = yoruba;
                      e.mediaAssetId = m_video.id;
                  });
    }

    void crudThemeElement()
    {
        checkCrud(Element{.themeId = m_theme.id,
                          .type = ElementType::Clock,
                          .frame = QRectF(1700, 40, 180, 60)},
                  [](Element &e) { e.type = ElementType::Timer; });
    }

    void crudTheme()
    {
        checkCrud(Theme{.name = "Scripture",
                        .appliesTo = ThemeTarget::Scripture,
                        .background = {{"colour", "#14213D"}},
                        .safeArea = QRectF(0.05, 0.05, 0.9, 0.9)},
                  [](Theme &e) {
                      e.isDefault = true; // batch 8
                      e.appliesTo = ThemeTarget::LowerThird;
                      e.background = {{"image", "/media/bg.png"}};
                      e.safeArea = QRectF(0.1, 0.7, 0.8, 0.25);
                  });
    }

    void crudMediaAsset()
    {
        checkCrud(MediaAsset{.libraryId = m_library.id,
                             .filePath = "/media/offering.mov",
                             .hash = "sha256:1f2e",
                             .durationMs = 7'200'000,
                             .thumbnailPath = "/cache/offering.jpg",
                             .loop = true,
                             .trimInMs = 1500,
                             .trimOutMs = 7'000'000},
                  [](MediaAsset &e) {
                      e.filePath = "/media/Ìjọsìn.mov";
                      e.loop = false;
                      e.trimOutMs = 0;
                  });
    }

    void crudPlan()
    {
        checkCrud(Plan{.name = "Evening Service", .date = QDate(2026, 12, 6)},
                  [](Plan &e) {
                      e.name = igbo;
                      e.date = QDate();
                  });
    }

    void crudPlanItem()
    {
        const Arrangement sunday = make<Arrangement>(
            {.presentationId = m_song.id, .name = "Sunday", .groupIds = {m_verse.id}});
        checkCrud(PlanItem{.planId = m_plan.id,
                           .position = 3,
                           .presentationId = m_song.id,
                           .arrangementId = sunday.id,
                           .notes = "Key of G"},
                  [](PlanItem &e) {
                      e.position = 0;
                      e.presentationId.clear();
                      e.arrangementId.clear();
                      e.headerText = "Worship";
                  });
    }

    void crudOutput()
    {
        checkCrud(Output{.name = "Main",
                         .target = "HDMI-1",
                         .layers = {{"slide", true}, {"background", true}}},
                  [&](Output &e) {
                      e.name = "Stage";
                      e.width = 3840;
                      e.height = 2160;
                      e.frameRate = 30;
                      e.layout = OutputLayout::Stage;
                      e.stageLayoutId = m_stage.id;
                  });
    }

    void crudStageLayout()
    {
        checkCrud(StageLayout{.name = "Pastor",
                              .widgets = {QJsonObject{{"type", "current"}},
                                          QJsonObject{{"type", "clock"}}}},
                  [](StageLayout &e) {
                      e.widgets.append(QJsonObject{{"type", "notes"}});
                  });
    }

    void crudMessage()
    {
        checkCrud(Message{.name = "Nursery",
                          .templateText = "Parent of {number}, please come to the nursery",
                          .fields = {QJsonObject{{"name", "number"}}}},
                  [](Message &e) { e.templateText = hausa + QStringLiteral(" {number}"); });
    }

    void crudTimer()
    {
        checkCrud(Timer{.name = "Pre-service", .mode = TimerMode::Countdown, .durationMs = 300'000},
                  [](Timer &e) {
                      e.mode = TimerMode::CountdownToTime;
                      e.durationMs = 0;
                      e.targetTime = QTime(10, 30);
                  });
    }

    // Batch 14.
    void crudProp()
    {
        checkCrud(Prop{.name = "Pastor", .kind = PropKind::LowerThird,
                       .data = {{"line1", "Pastor Ade Johnson"}, {"line2", "Lead pastor"}}},
                  [](Prop &e) {
                      e.kind = PropKind::Bug;
                      e.data = {{"image", "/logos/church.png"}, {"corner", "topRight"}, {"size", 0.12}};
                  });
    }

    // ---- ordering -----------------------------------------------------

    void childrenListInPositionOrder()
    {
        const Group chorus = make<Group>({.presentationId = m_song.id, .position = 2, .label = "Chorus"});
        const Group bridge = make<Group>({.presentationId = m_song.id, .position = 1, .label = "Bridge"});
        // m_verse is at position 0.
        auto groups = Repository<Group>(*m_db).listChildren(m_song.id);
        QVERIFY2(groups.ok(), why(groups));
        QCOMPARE(ids(groups.value()), (QStringList{m_verse.id, bridge.id, chorus.id}));

        const Slide third = make<Slide>({.groupId = m_verse.id, .position = 2});
        const Slide second = make<Slide>({.groupId = m_verse.id, .position = 1});
        auto slides = Repository<Slide>(*m_db).listChildren(m_verse.id);
        QVERIFY2(slides.ok(), why(slides));
        QCOMPARE(ids(slides.value()), (QStringList{m_slide.id, second.id, third.id}));

        const PlanItem b = make<PlanItem>({.planId = m_plan.id, .position = 1, .headerText = "B"});
        const PlanItem a = make<PlanItem>({.planId = m_plan.id, .position = 0, .headerText = "A"});
        auto items = Repository<PlanItem>(*m_db).listChildren(m_plan.id);
        QVERIFY2(items.ok(), why(items));
        QCOMPARE(ids(items.value()), (QStringList{a.id, b.id}));
    }

    void elementsListBySlideOrThemeInZOrder()
    {
        const Element top = make<Element>({.slideId = m_slide.id, .zOrder = 5});
        const Element bottom = make<Element>({.slideId = m_slide.id, .zOrder = 1});
        const Element themed = make<Element>({.themeId = m_theme.id, .type = ElementType::Clock});

        auto onSlide = Repository<Element>(*m_db).listChildren(m_slide.id);
        QVERIFY2(onSlide.ok(), why(onSlide));
        QCOMPARE(ids(onSlide.value()), (QStringList{bottom.id, top.id}));

        auto onTheme = Repository<Element>(*m_db).listChildren(m_theme.id);
        QVERIFY2(onTheme.ok(), why(onTheme));
        QCOMPARE(ids(onTheme.value()), QStringList{themed.id});
    }

    void rootEntitiesHaveNoChildren()
    {
        auto result = Repository<Library>(*m_db).listChildren(m_library.id);
        QVERIFY(!result.ok());
        QCOMPARE(result.error().code, Error::Code::Invalid);
    }

    void arrangementKeepsRepeatedGroupsInOrder()
    {
        const Group chorus = make<Group>({.presentationId = m_song.id, .position = 1, .label = "Chorus"});
        const Group bridge = make<Group>({.presentationId = m_song.id, .position = 2, .label = "Bridge"});
        // V1 C V1 C B C
        const QStringList order{m_verse.id, chorus.id, m_verse.id, chorus.id, bridge.id, chorus.id};
        const Arrangement arrangement =
            make<Arrangement>({.presentationId = m_song.id, .name = "Full", .groupIds = order});

        auto listed = Repository<Arrangement>(*m_db).listChildren(m_song.id);
        QVERIFY2(listed.ok(), why(listed));
        QCOMPARE(listed.value().size(), std::size_t(1));
        QCOMPARE(listed.value().front().groupIds, order);

        // Deleting a group takes it out of every arrangement.
        QVERIFY(Repository<Group>(*m_db).remove(chorus.id).ok());
        auto after = Repository<Arrangement>(*m_db).get(arrangement.id);
        QVERIFY2(after.ok(), why(after));
        QCOMPARE(after.value().groupIds, (QStringList{m_verse.id, m_verse.id, bridge.id}));
    }

    void onlyOneDefaultArrangementPerPresentation()
    {
        make<Arrangement>({.presentationId = m_song.id, .name = "Default", .isDefault = true});
        auto second = Repository<Arrangement>(*m_db).create(
            {.presentationId = m_song.id, .name = "Another", .isDefault = true});
        QVERIFY(!second.ok());
        QCOMPARE(second.error().code, Error::Code::Constraint);
    }

    // ---- delete rules -------------------------------------------------

    void deletingLibraryDeletesWhatItOwns()
    {
        const Element element = make<Element>({.slideId = m_slide.id, .mediaAssetId = m_video.id});
        const Arrangement arrangement =
            make<Arrangement>({.presentationId = m_song.id, .groupIds = {m_verse.id}});

        auto removed = Repository<Library>(*m_db).remove(m_library.id);
        QVERIFY2(removed.ok(), why(removed));
        QVERIFY(!exists<Presentation>(m_song.id));
        QVERIFY(!exists<Group>(m_verse.id));
        QVERIFY(!exists<Arrangement>(arrangement.id));
        QVERIFY(!exists<Slide>(m_slide.id));
        QVERIFY(!exists<Element>(element.id));
        QVERIFY(!exists<MediaAsset>(m_video.id));
        // Themes and plans do not belong to a library.
        QVERIFY(exists<Theme>(m_theme.id));
        QVERIFY(exists<Plan>(m_plan.id));
    }

    void deletingThemeClearsReferencesAndItsElements()
    {
        const Element themed = make<Element>({.themeId = m_theme.id});
        QVERIFY(Repository<Theme>(*m_db).remove(m_theme.id).ok());
        auto song = Repository<Presentation>(*m_db).get(m_song.id);
        QVERIFY2(song.ok(), why(song));
        QVERIFY(song.value().themeId.isEmpty());
        QVERIFY(!exists<Element>(themed.id));
    }

    void deletingArrangementClearsPlanItemReference()
    {
        const Arrangement arrangement = make<Arrangement>({.presentationId = m_song.id});
        const PlanItem item = make<PlanItem>(
            {.planId = m_plan.id, .presentationId = m_song.id, .arrangementId = arrangement.id});
        QVERIFY(Repository<Arrangement>(*m_db).remove(arrangement.id).ok());
        auto after = Repository<PlanItem>(*m_db).get(item.id);
        QVERIFY2(after.ok(), why(after));
        QVERIFY(after.value().arrangementId.isEmpty());
        QCOMPARE(after.value().presentationId, m_song.id);
    }

    void deletingStageLayoutClearsOutputReference()
    {
        const Output output = make<Output>(
            {.name = "Stage", .layout = OutputLayout::Stage, .stageLayoutId = m_stage.id});
        QVERIFY(Repository<StageLayout>(*m_db).remove(m_stage.id).ok());
        auto after = Repository<Output>(*m_db).get(output.id);
        QVERIFY2(after.ok(), why(after));
        QVERIFY(after.value().stageLayoutId.isEmpty());
    }

    void deletingPlanDeletesItsItems()
    {
        const PlanItem item = make<PlanItem>({.planId = m_plan.id, .presentationId = m_song.id});
        QVERIFY(Repository<Plan>(*m_db).remove(m_plan.id).ok());
        QVERIFY(!exists<PlanItem>(item.id));
        QVERIFY(exists<Presentation>(m_song.id));
    }

    // A song must not silently vanish from Sunday's plan.
    void deletingPresentationInAPlanIsBlocked()
    {
        const PlanItem item = make<PlanItem>({.planId = m_plan.id, .presentationId = m_song.id});

        auto blocked = Repository<Presentation>(*m_db).remove(m_song.id);
        QVERIFY(!blocked.ok());
        QCOMPARE(blocked.error().code, Error::Code::Constraint);
        QVERIFY(exists<Presentation>(m_song.id));
        QVERIFY(exists<Slide>(m_slide.id));

        // The same applies when deleting the whole library.
        auto libraryBlocked = Repository<Library>(*m_db).remove(m_library.id);
        QVERIFY(!libraryBlocked.ok());
        QCOMPARE(libraryBlocked.error().code, Error::Code::Constraint);
        QVERIFY(exists<Presentation>(m_song.id));

        QVERIFY(Repository<PlanItem>(*m_db).remove(item.id).ok());
        QVERIFY(Repository<Presentation>(*m_db).remove(m_song.id).ok());
    }

    void deletingMediaUsedByAnElementIsBlocked()
    {
        const Element element = make<Element>(
            {.slideId = m_slide.id, .type = ElementType::Video, .mediaAssetId = m_video.id});
        auto blocked = Repository<MediaAsset>(*m_db).remove(m_video.id);
        QVERIFY(!blocked.ok());
        QCOMPARE(blocked.error().code, Error::Code::Constraint);
        QVERIFY(exists<MediaAsset>(m_video.id));

        QVERIFY(Repository<Element>(*m_db).remove(element.id).ok());
        QVERIFY(Repository<MediaAsset>(*m_db).remove(m_video.id).ok());
    }

    // ---- rules the schema enforces ------------------------------------

    void referencesMustExist()
    {
        auto orphan = Repository<Group>(*m_db).create({.presentationId = "no-such-presentation"});
        QVERIFY(!orphan.ok());
        QCOMPARE(orphan.error().code, Error::Code::Constraint);

        Slide moved = m_slide;
        moved.groupId = QStringLiteral("no-such-group");
        auto update = Repository<Slide>(*m_db).update(moved);
        QVERIFY(!update.ok());
        QCOMPARE(update.error().code, Error::Code::Constraint);
    }

    void elementBelongsToExactlyOneOwner()
    {
        auto neither = Repository<Element>(*m_db).create({});
        QVERIFY(!neither.ok());
        QCOMPARE(neither.error().code, Error::Code::Constraint);

        auto both = Repository<Element>(*m_db).create({.slideId = m_slide.id, .themeId = m_theme.id});
        QVERIFY(!both.ok());
        QCOMPARE(both.error().code, Error::Code::Constraint);
    }

    void planItemNeedsPresentationOrHeader()
    {
        auto empty = Repository<PlanItem>(*m_db).create({.planId = m_plan.id});
        QVERIFY(!empty.ok());
        QCOMPARE(empty.error().code, Error::Code::Constraint);
    }

    void duplicateIdIsRefused()
    {
        auto copy = Repository<Library>(*m_db).create({.id = m_library.id, .name = "Copy"});
        QVERIFY(!copy.ok());
        QCOMPARE(copy.error().code, Error::Code::Constraint);
    }

    void failedArrangementWriteChangesNothing()
    {
        const Arrangement arrangement =
            make<Arrangement>({.presentationId = m_song.id, .name = "Short", .groupIds = {m_verse.id}});
        Arrangement broken = arrangement;
        broken.name = QStringLiteral("Broken");
        broken.groupIds = {m_verse.id, QStringLiteral("no-such-group")};
        QVERIFY(!Repository<Arrangement>(*m_db).update(broken).ok());

        auto after = Repository<Arrangement>(*m_db).get(arrangement.id);
        QVERIFY2(after.ok(), why(after));
        QVERIFY(after.value() == arrangement);
    }

    // ---- text ----------------------------------------------------------

    // Qt binds a null QString as SQL NULL; empty text must be stored as ''
    // while an empty reference id must be stored as NULL.
    void emptyTextIsEmptyAndEmptyReferenceIsNull()
    {
        const Group bare = make<Group>({.presentationId = m_song.id});
        const Presentation plain = make<Presentation>({.libraryId = m_library.id});
        QSqlQuery query(m_db->connection());
        QVERIFY(query.exec(QStringLiteral("SELECT colour, hotkey, label FROM slide_groups WHERE id = '%1'")
                               .arg(bare.id)));
        QVERIFY(query.next());
        for (int column = 0; column < 3; ++column) {
            QVERIFY(!query.value(column).isNull());
            QCOMPARE(query.value(column).toString(), QString());
        }
        QVERIFY(query.exec(QStringLiteral("SELECT theme_id, title FROM presentations WHERE id = '%1'")
                               .arg(plain.id)));
        QVERIFY(query.next());
        QVERIFY(query.value(0).isNull());
        QVERIFY(!query.value(1).isNull());
    }

    void africanLanguageTextRoundTrips()
    {
        const QStringList texts{yoruba, igbo, hausa};
        for (const QString &text : texts) {
            const Presentation song = make<Presentation>(
                {.libraryId = m_library.id, .title = text, .metadata = {{"author", text}}});
            auto fetched = Repository<Presentation>(*m_db).get(song.id);
            QVERIFY2(fetched.ok(), why(fetched));
            QCOMPARE(fetched.value().title, text);
            QCOMPARE(fetched.value().metadata.value(QStringLiteral("author")).toString(), text);
        }
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        TestRepositories onDisk(false);
        status |= QTest::qExec(&onDisk, argc, argv);
    }
    {
        TestRepositories inMemory(true);
        status |= QTest::qExec(&inMemory, argc, argv);
    }
    return status;
}

#include "tst_repositories.moc"
