#include "sampleservice.h"

#include "lumen/core/data/repository.h"

#include <QJsonArray>
#include <QJsonObject>

#include <utility>
#include <vector>

namespace lumen::app {

using namespace core;
using namespace core::data;

namespace {

struct SampleGroup
{
    QString label;
    QString colour;
    QStringList slides;
};

struct SampleItem
{
    PresentationKind kind;
    QString title;
    QJsonObject metadata;
    std::vector<SampleGroup> groups;
    bool timer = false;
    bool themed = false;
};

// Stops at the first failure and remembers it.
class Seeder
{
public:
    explicit Seeder(Database &db) : m_db(db) {}

    template <typename T>
    T add(T entity)
    {
        if (!m_error.message.isEmpty())
            return entity;
        auto created = Repository<T>(m_db).create(std::move(entity));
        if (!created.ok()) {
            m_error = created.error();
            return {};
        }
        return created.value();
    }

    Presentation addItem(const Library &library, const Theme &theme, const SampleItem &item)
    {
        Presentation p = add(Presentation{.libraryId = library.id,
                                          .kind = item.kind,
                                          .title = item.title,
                                          .themeId = item.themed ? theme.id : QString(),
                                          .metadata = item.metadata});
        QStringList order;
        int groupPosition = 0;
        for (const SampleGroup &g : item.groups) {
            const Group group = add(Group{.presentationId = p.id,
                                          .position = groupPosition++,
                                          .label = g.label,
                                          .colour = g.colour});
            order << group.id;
            int slidePosition = 0;
            for (const QString &text : g.slides) {
                const Slide slide = add(Slide{.groupId = group.id, .position = slidePosition++});
                add(Element{.slideId = slide.id,
                            .type = ElementType::Text,
                            .frame = QRectF(96, 96, 1728, 888),
                            .content = text});
                if (item.timer)
                    add(Element{.slideId = slide.id, .type = ElementType::Timer, .zOrder = 1});
            }
        }
        if (item.kind == PresentationKind::Song)
            add(Arrangement{.presentationId = p.id, .name = "Default", .isDefault = true, .groupIds = order});
        return p;
    }

    Status status() const { return m_error.message.isEmpty() ? success() : Status(m_error); }

private:
    Database &m_db;
    Error m_error;
};

QJsonObject song(const QString &author, const QString &key)
{
    return {{"author", author}, {"copyright", "Public domain"}, {"key", key}};
}

} // namespace

Status seedSampleService(Database &db)
{
    Seeder seed(db);
    const Library library = seed.add(Library{.name = "Main library"});
    const Theme lyrics = seed.add(Theme{.name = "Lyrics, centred", .appliesTo = ThemeTarget::Song});

    const std::vector<SampleItem> service{
        {PresentationKind::Slides, "Pre-service countdown", {{"subtitle", "Announcement loop"}},
         {{"Countdown", "", {"Service starts soon"}}}, true},
        {PresentationKind::Slides, "Welcome", {{"subtitle", "Native slides"}},
         {{"Welcome", "", {"Welcome to Grace Church", "[Announcement]"}}}},
        {PresentationKind::Song, "Amazing Grace", song("John Newton", "G"),
         {{"Verse 1", "#5B8DEF",
           {"Amazing grace! How sweet the sound\nThat saved a wretch like me!",
            "I once was lost, but now am found;\nWas blind, but now I see."}},
          {"Verse 2", "#3FB7A6",
           {"'Twas grace that taught my heart to fear,\nAnd grace my fears relieved;",
            "How precious did that grace appear\nThe hour I first believed."}},
          {"Verse 3", "#A779E9",
           {"Through many dangers, toils and snares,\nI have already come;",
            "'Tis grace hath brought me safe thus far,\nAnd grace will lead me home."}},
          {"Verse 4", "#E8833A",
           {"When we've been there ten thousand years,\nBright shining as the sun,",
            "We've no less days to sing God's praise\nThan when we'd first begun."}},
          {"Tag", "#C9CED6", {"Was blind, but now I see."}}},
         false, true},
        {PresentationKind::Song, "Holy, Holy, Holy", song("Reginald Heber", "D"),
         {{"Verse 1", "",
           {"Holy, holy, holy! Lord God Almighty!\nEarly in the morning our song shall rise to Thee;",
            "Holy, holy, holy, merciful and mighty!\nGod in three Persons, blessèd Trinity!"}}},
         false, true},
        {PresentationKind::Scripture, "John 3:16-18", {{"subtitle", "World English Bible"}},
         {{"John 3:16", "",
           {"For God so loved the world, that he gave his one and only Son, that whoever believes "
            "in him should not perish, but have eternal life."}},
          {"John 3:17", "",
           {"For God didn't send his Son into the world to judge the world, but that the world "
            "should be saved through him."}},
          {"John 3:18", "",
           {"He who believes in him is not judged. He who doesn't believe has been judged already, "
            "because he has not believed in the name of the one and only Son of God."}}}},
        {PresentationKind::Slides, "Announcements", {{"subtitle", "PowerPoint import"}},
         {{"Announcements", "", {"[Announcement 1]", "[Announcement 2]"}}}},
        {PresentationKind::Media, "Offering video", {{"subtitle", "[Video file]"}},
         {{"Video", "", {"[Video file]"}}}},
        {PresentationKind::Slides, "[Sermon title]", {{"subtitle", "Sermon points"}},
         {{"Sermon", "", {"[Sermon title]", "[Point 1]", "[Point 2]"}}}},
        {PresentationKind::Song, "Be Thou My Vision",
         {{"author", "Trad. Irish, tr. Mary Byrne"}, {"subtitle", "Trad. Irish"},
          {"copyright", "Public domain"}, {"key", "Eb"}},
         {{"Verse 1", "",
           {"Be Thou my Vision, O Lord of my heart;\nNaught be all else to me, save that Thou art.",
            "Thou my best Thought, by day or by night,\nWaking or sleeping, Thy presence my light."}}},
         false, true},
    };

    const Plan plan = seed.add(Plan{.name = "Sunday Service", .date = QDate::currentDate()});
    int position = 0;
    for (const SampleItem &item : service) {
        const Presentation p = seed.addItem(library, lyrics, item);
        seed.add(PlanItem{.planId = plan.id, .position = position++, .presentationId = p.id});
    }
    // In the library but not in today's plan.
    seed.addItem(library, lyrics,
                 {PresentationKind::Song, "It Is Well with My Soul", song("Horatio Spafford", "C"),
                  {{"Verse 1", "",
                    {"When peace like a river attendeth my way,\nWhen sorrows like sea billows roll;"}}},
                  false, true});
    return seed.status();
}

} // namespace lumen::app
