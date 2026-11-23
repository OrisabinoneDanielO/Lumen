#include "servicedata.h"

#include "themes.h"

#include "lumen/core/data/repository.h"

#include <QRegularExpression>

#include <algorithm>

namespace lumen::app {

using namespace core;
using namespace core::data;

namespace {

QColor blend(const QColor &a, const QColor &b, double amountOfB)
{
    auto lerp = [amountOfB](int x, int y) { return int(x + (y - x) * amountOfB + 0.5); };
    return QColor(lerp(a.red(), b.red()), lerp(a.green(), b.green()), lerp(a.blue(), b.blue()));
}

QString kindLabel(const Presentation &presentation, bool hasTimer)
{
    switch (presentation.kind) {
    case PresentationKind::Song: return QStringLiteral("SONG");
    case PresentationKind::Scripture: return QStringLiteral("BIBLE");
    case PresentationKind::Media: return QStringLiteral("MEDIA");
    case PresentationKind::Slides: break;
    }
    return hasTimer ? QStringLiteral("TIMER") : QStringLiteral("SLIDES");
}

// Group ids in play order: the plan item's arrangement, else the default
// arrangement, else every group in position order.
Result<QStringList> playOrder(Database &db, const QString &presentationId,
                              const QString &arrangementId, const std::vector<Group> &groups)
{
    Repository<Arrangement> arrangements(db);
    if (!arrangementId.isEmpty()) {
        auto chosen = arrangements.get(arrangementId);
        if (chosen.ok())
            return chosen.value().groupIds;
    }
    auto all = arrangements.listChildren(presentationId);
    if (!all.ok())
        return all.error();
    const auto &list = all.value();
    const auto preferred = std::find_if(list.begin(), list.end(),
                                        [](const Arrangement &a) { return a.isDefault; });
    if (preferred != list.end())
        return preferred->groupIds;
    if (!list.empty())
        return list.front().groupIds;

    QStringList order;
    for (const Group &group : groups)
        order << group.id;
    return order;
}

Result<ItemView> loadItem(Database &db, const PlanItem &item)
{
    if (item.presentationId.isEmpty()) {
        ItemView header;
        header.planItemId = item.id;
        header.title = item.headerText;
        return header;
    }
    auto view = loadPresentationView(db, item.presentationId, item.arrangementId);
    if (view.ok())
        view.value().planItemId = item.id;
    return view;
}

} // namespace

Result<ItemView> loadPresentationView(Database &db, const QString &presentationId,
                                      const QString &arrangementId)
{
    ItemView view;
    view.presentationId = presentationId;
    auto presentation = Repository<Presentation>(db).get(presentationId);
    if (!presentation.ok())
        return presentation.error();
    const Presentation &p = presentation.value();
    const QJsonObject &meta = p.metadata;

    view.title = p.title;
    const QString author = meta.value(QStringLiteral("author")).toString();
    const QString subtitle = meta.value(QStringLiteral("subtitle")).toString();
    view.subtitle = subtitle.isEmpty() ? author : subtitle;

    QStringList detail;
    if (!author.isEmpty())
        detail << author;
    if (const QString copyright = meta.value(QStringLiteral("copyright")).toString(); !copyright.isEmpty())
        detail << copyright;
    if (!p.themeId.isEmpty()) {
        if (auto theme = Repository<Theme>(db).get(p.themeId); theme.ok())
            detail << QStringLiteral("Theme: ") + theme.value().name;
    }
    view.detail = detail.join(QStringLiteral(" · "));

    auto groups = Repository<Group>(db).listChildren(p.id);
    if (!groups.ok())
        return groups.error();
    auto order = playOrder(db, p.id, arrangementId, groups.value());
    if (!order.ok())
        return order.error();

    bool hasTimer = false;
    Repository<Slide> slides(db);
    Repository<Element> elements(db);
    for (const QString &groupId : order.value()) {
        const auto group = std::find_if(groups.value().begin(), groups.value().end(),
                                        [&](const Group &g) { return g.id == groupId; });
        if (group == groups.value().end())
            continue;
        const QColor colour = QColor::fromString(group->colour).isValid()
                                  ? QColor::fromString(group->colour)
                                  : defaultGroupColour(group->label);
        view.chips.push_back({groupAbbreviation(group->label), blend(colour, QColor(0x17, 0x1A, 0x20), 0.72),
                              blend(colour, Qt::white, 0.6)});

        auto groupSlides = slides.listChildren(group->id);
        if (!groupSlides.ok())
            return groupSlides.error();
        for (const Slide &slide : groupSlides.value()) {
            auto slideElements = elements.listChildren(slide.id);
            if (!slideElements.ok())
                return slideElements.error();
            QStringList lines;
            SlideView media;
            for (const Element &element : slideElements.value()) {
                if (element.type == ElementType::Text && !element.content.isEmpty())
                    lines << element.content;
                hasTimer = hasTimer || element.type == ElementType::Timer;
                if ((element.type == ElementType::Video || element.type == ElementType::Image)
                    && !element.mediaAssetId.isEmpty() && media.mediaKind.isEmpty()) {
                    if (auto asset = Repository<MediaAsset>(db).get(element.mediaAssetId); asset.ok()) {
                        media.mediaKind = element.type == ElementType::Video ? QStringLiteral("video") : QStringLiteral("image");
                        media.mediaPath = asset.value().filePath;
                        media.thumbnail = asset.value().thumbnailPath;
                    }
                }
            }
            view.slides.push_back({slide.id, group->label, colour, lines.join(u'\n'), {}, 0,
                                   media.mediaKind, media.mediaPath, media.thumbnail});
        }
    }
    view.kind = kindLabel(p, hasTimer);

    // Footers: songs credit the song; scripture names the passage and the
    // translation's copyright line.
    if (p.kind == PresentationKind::Song) {
        QStringList credit{p.title};
        for (const char *field : {"author", "copyright"}) {
            const QString value = meta.value(QLatin1String(field)).toString();
            if (!value.isEmpty())
                credit << value;
        }
        for (SlideView &slide : view.slides)
            slide.footer = credit.join(QStringLiteral(" · "));
    } else if (p.kind == PresentationKind::Scripture) {
        static const QRegularExpression part(QStringLiteral(R"(\s*\(\d+/\d+\)$)"));
        const QString abbreviation = meta.value(QStringLiteral("abbreviation")).toString();
        const QString copyright = meta.value(QStringLiteral("copyright")).toString();
        for (SlideView &slide : view.slides) {
            QString reference = slide.group;
            reference.remove(part);
            slide.footer = abbreviation.isEmpty() ? reference : QStringLiteral("%1 (%2)").arg(reference, abbreviation);
            if (!copyright.isEmpty())
                slide.footer += QStringLiteral(" · ") + copyright;
        }
    }

    if (const auto theme = themeFor(db, p))
        applyTheme(view, theme->style, theme->background);
    return view;
}

void applyTheme(ItemView &item, const core::show::TextStyle &style, const QJsonObject &background)
{
    item.style = style;
    item.background = background;
    for (SlideView &slide : item.slides)
        slide.fontSize = core::show::fitText(slide.text, style, !slide.footer.isEmpty()).pixelSize;
}

QColor defaultGroupColour(const QString &label)
{
    const QString l = label.toLower();
    if (l.startsWith(QLatin1String("verse")))
        return QColor(0x5B, 0x8D, 0xEF);
    if (l.startsWith(QLatin1String("pre")))
        return QColor(0xE8, 0x83, 0x3A);
    if (l.startsWith(QLatin1String("chorus")))
        return QColor(0xE5, 0x48, 0x4D);
    if (l.startsWith(QLatin1String("bridge")))
        return QColor(0xA7, 0x79, 0xE9);
    if (l.startsWith(QLatin1String("tag")) || l.startsWith(QLatin1String("ending")))
        return QColor(0xC9, 0xCE, 0xD6);
    return QColor(0x8A, 0x93, 0xA3);
}

QString groupAbbreviation(const QString &label)
{
    const QString l = label.trimmed();
    const QString lower = l.toLower();
    const QString number = l.section(u' ', -1);
    const bool numbered = l.contains(u' ') && !number.isEmpty() && number.at(0).isDigit();
    if (lower.startsWith(QLatin1String("verse")))
        return QStringLiteral("V") + (numbered ? number : QString());
    if (lower.startsWith(QLatin1String("pre")))
        return QStringLiteral("PC");
    if (lower.startsWith(QLatin1String("chorus")))
        return QStringLiteral("C") + (numbered ? number : QString());
    if (lower.startsWith(QLatin1String("bridge")))
        return QStringLiteral("B");
    if (lower.startsWith(QLatin1String("ending")))
        return QStringLiteral("E");
    return l;
}

Result<PlanView> loadPlan(Database &db, const QString &planId)
{
    auto plan = Repository<Plan>(db).get(planId);
    if (!plan.ok())
        return plan.error();
    auto items = Repository<PlanItem>(db).listChildren(planId);
    if (!items.ok())
        return items.error();

    PlanView view{plan.value().id, plan.value().name, plan.value().date, {}};
    for (const PlanItem &item : items.value()) {
        auto loaded = loadItem(db, item);
        if (!loaded.ok())
            return loaded.error();
        view.items.push_back(std::move(loaded).value());
    }
    return view;
}

Result<std::optional<PlanView>> loadLatestPlan(Database &db)
{
    auto plans = Repository<Plan>(db).list(); // by date, then name
    if (!plans.ok())
        return plans.error();
    if (plans.value().empty())
        return std::optional<PlanView>();
    auto plan = loadPlan(db, plans.value().back().id);
    if (!plan.ok())
        return plan.error();
    return std::optional<PlanView>(std::move(plan).value());
}

Result<std::vector<SongRow>> loadSongs(Database &db)
{
    auto presentations = Repository<Presentation>(db).list(); // by title
    if (!presentations.ok())
        return presentations.error();
    std::vector<SongRow> rows;
    for (const Presentation &p : presentations.value()) {
        if (p.kind != PresentationKind::Song)
            continue;
        rows.push_back({p.title, p.metadata.value(QStringLiteral("author")).toString(),
                        p.metadata.value(QStringLiteral("copyright")).toString(),
                        p.metadata.value(QStringLiteral("key")).toString(), p.id});
    }
    return rows;
}

// ---- sample service --------------------------------------------------------

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
