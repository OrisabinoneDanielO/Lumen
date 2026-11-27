#include "servicedata.h"
#include "medialibrary.h"

#include <cmath>

#include <QUrl>

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
        header.notes = item.notes;
        return header;
    }
    auto view = loadPresentationView(db, item.presentationId, item.arrangementId);
    if (view.ok()) {
        view.value().planItemId = item.id;
        view.value().notes = item.notes;
    }
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
    view.key = meta.value(QStringLiteral("key")).toString();
    view.transpose = meta.value(QStringLiteral("transpose")).toInt();
    view.autoAdvance = meta.value(QStringLiteral("autoAdvance")).toInt();

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
            bool slideTimer = false;
            QString timerName;
            for (const Element &element : slideElements.value()) {
                if (element.type == ElementType::Text && !element.content.isEmpty())
                    lines << element.content;
                if (element.type == ElementType::Timer) {
                    slideTimer = true;
                    timerName = element.content;
                }
                if ((element.type == ElementType::Video || element.type == ElementType::Image)
                    && !element.mediaAssetId.isEmpty() && media.mediaKind.isEmpty()) {
                    if (auto asset = Repository<MediaAsset>(db).get(element.mediaAssetId); asset.ok()) {
                        media.mediaKind = element.type != ElementType::Video          ? QStringLiteral("image")
                                          : MediaLibrary::isAudio(asset.value().filePath) ? QStringLiteral("audio")
                                                                                          : QStringLiteral("video");
                        media.mediaPath = asset.value().filePath;
                        media.thumbnail = asset.value().thumbnailPath;
                    }
                }
            }
            SlideView made{slide.id, group->label, colour, lines.join(u'\n'), {}, 0,
                           media.mediaKind, media.mediaPath, media.thumbnail, slideTimer, timerName, {}};
            // The operator's choice for this video (Play once by default).
            made.mediaLoop = p.metadata.value(QStringLiteral("videoLoop")).toBool();
            if (made.mediaKind == QLatin1String("audio") && made.text.isEmpty())
                made.text = QObject::tr("Audio: %1").arg(p.title); // what its card says
            // Chords as written inline (older imports kept only the names).
            if (slide.chords.contains(u'['))
                made.chords = slide.chords;
            made.trimInMs = p.metadata.value(QStringLiteral("trimInMs")).toInteger();
            made.trimOutMs = p.metadata.value(QStringLiteral("trimOutMs")).toInteger();
            // Slides made in the editor; scripture with a second translation
            // side by side.
            if ((p.kind == PresentationKind::Slides || p.kind == PresentationKind::Scripture)
                && isComposed(slideElements.value())) {
                // Drawn from its elements, not as text, picture or countdown.
                made.elements = composedElements(db, slideElements.value());
                made.mediaKind.clear();
                made.mediaPath.clear();
                made.hasTimer = false;
            }
            // A countdown slide (not one made in the editor) makes the item
            // a TIMER in the plan.
            hasTimer = hasTimer || made.hasTimer;
            view.slides.push_back(std::move(made));
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
        // Copyright line hidden when it was added: the required credit on
        // the first slide only.
        const bool firstOnly = meta.value(QStringLiteral("creditFirstSlideOnly")).toBool();
        for (SlideView &slide : view.slides) {
            QString reference = slide.group;
            reference.remove(part);
            slide.footer = abbreviation.isEmpty() ? reference : QStringLiteral("%1 (%2)").arg(reference, abbreviation);
            if (!copyright.isEmpty() && (!firstOnly || &slide == &view.slides.front()))
                slide.footer += QStringLiteral(" · ") + copyright;
        }
    }

    if (const auto theme = themeFor(db, p)) {
        // Scripture keeps the reference where it was put.
        const QString position = meta.value(QStringLiteral("referencePosition")).toString();
        const core::show::TextStyle style =
            p.kind == PresentationKind::Scripture ? scriptureStyle(theme->style, position) : theme->style;
        applyTheme(view, style, theme->background);
        // Side by side scripture, in the current layout (also when stored
        // in an older one) and this theme.
        if (p.kind == PresentationKind::Scripture) {
            for (SlideView &slide : view.slides) {
                if (!slide.elements.isEmpty())
                    relayoutSideBySide(slide, style, meta);
            }
        }
    }
    return view;
}

void applyTheme(ItemView &item, const core::show::TextStyle &style, const QJsonObject &background)
{
    item.style = style;
    item.background = background;
    for (SlideView &slide : item.slides)
        slide.fontSize = core::show::fitText(slide.text, style, !slide.footer.isEmpty()).pixelSize;
}

core::show::TextStyle scriptureStyle(core::show::TextStyle style, const QString &position)
{
    if (position == QLatin1String("above")) {
        style.footerPosition = QStringLiteral("above");
    } else if (position == QLatin1String("lowerThird")) {
        style.footerPosition = QStringLiteral("below");
        style.safeArea = QRectF(0.05, 0.62, 0.9, 0.33);
        style.valign = QStringLiteral("bottom");
        style.maxSize = qMin(style.maxSize, 52);
        style.minSize = qMin(style.minSize, 32);
        style.footerSize = qMin(style.footerSize, 26);
    } else if (position == QLatin1String("below")) {
        style.footerPosition = QStringLiteral("below");
    }
    return style;
}

QJsonArray sideBySideElements(const core::show::TextStyle &style, const QString &left, const QString &leftReference,
                              const QString &leftCredit, const QString &right, const QString &rightReference,
                              const QString &rightCredit)
{
    using core::show::TextStyle;
    // No footer band: each column carries its reference and credit under
    // its passage, and the whole block sits where the theme puts a slide's
    // text (centred, top or bottom), like a normal scripture slide.
    TextStyle plain = style;
    plain.footerPosition = QStringLiteral("none");
    const auto areas = core::show::textAreas(plain, core::show::referenceSize, false);
    const auto colour = [](const QColor &c) { return c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb); };
    const auto frame = [](const QRectF &r) {
        return QJsonArray{std::round(r.x()), std::round(r.y()), std::round(r.width()), std::round(r.height())};
    };
    const bool above = style.footerPosition == QLatin1String("above");
    const bool credits = !leftCredit.isEmpty() || !rightCredit.isEmpty();
    const double gap = 64;
    const double column = (areas.body.width() - gap) / 2;
    const double referenceHeight = std::ceil(style.footerSize * 1.5);
    const int creditSize = qMax(14, style.footerSize * 2 / 3);
    const double creditHeight = credits ? std::ceil(creditSize * 1.3 * 2) : 0; // room for two lines
    const double referenceGap = std::ceil(style.footerSize * 0.4);
    const double captions = referenceHeight + creditHeight; // reference, then credit, per column

    // One size for both columns: the largest at which each fits beside the
    // other (with room for its captions).
    TextStyle columnStyle = plain;
    columnStyle.safeArea = QRectF(areas.body.left() / core::show::referenceSize.width(),
                                  areas.body.top() / core::show::referenceSize.height(),
                                  column / core::show::referenceSize.width(),
                                  (areas.body.height() - captions - referenceGap) / core::show::referenceSize.height());
    const int size = qMin(core::show::fitText(left, columnStyle, false).pixelSize,
                          core::show::fitText(right, columnStyle, false).pixelSize);
    const double wrapWidth = column * 0.97 - 2.0 * style.outline;
    const double textHeight = std::min(areas.body.height() - captions - referenceGap,
                                       std::ceil(std::max(core::show::textHeight(left, style, size, wrapWidth),
                                                          core::show::textHeight(right, style, size, wrapWidth)))
                                           + 4);
    const double block = textHeight + referenceGap + captions;
    const double top = style.valign == QLatin1String("top")      ? areas.body.top()
                       : style.valign == QLatin1String("bottom") ? areas.body.bottom() - block
                                                                 : areas.body.top() + (areas.body.height() - block) / 2;
    // Captions under the passages (or, reference above, over them).
    const double textTop = above ? top + captions + referenceGap : top;
    const double referenceTop = above ? top : top + textHeight + referenceGap;
    const double creditTop = referenceTop + referenceHeight;
    const auto columnAt = [&](int i) { return areas.body.left() + i * (column + gap); };

    const QJsonObject textStyle{{"font", style.font},
                                {"size", size},
                                {"bold", style.weight >= 600},
                                {"colour", colour(style.colour)},
                                {"align", style.align},
                                {"valign", "top"}, // both columns start on the same line
                                {"shadow", style.shadow},
                                {"outline", style.outline},
                                {"outlineColour", colour(style.outlineColour)},
                                {"lineHeight", style.lineHeight},
                                {"fit", false}};
    QJsonObject referenceStyle = textStyle;
    referenceStyle.insert("size", style.footerSize);
    referenceStyle.insert("colour", colour(style.footerColour));
    referenceStyle.insert("bold", true);
    referenceStyle.insert("lineHeight", 1.0);
    referenceStyle.insert("fit", true);
    QJsonObject creditStyle = referenceStyle;
    creditStyle.insert("size", creditSize);
    creditStyle.insert("bold", false);

    QJsonArray elements;
    for (const auto &[i, text] : {std::pair(0, left), std::pair(1, right)})
        elements.append(QJsonObject{{"type", "text"},
                                    {"frame", frame(QRectF(columnAt(i), textTop, column, textHeight))},
                                    {"style", textStyle},
                                    {"text", text}});
    for (const auto &[i, reference] : {std::pair(0, leftReference), std::pair(1, rightReference)})
        elements.append(QJsonObject{{"type", "text"},
                                    {"frame", frame(QRectF(columnAt(i), referenceTop, column, referenceHeight))},
                                    {"style", referenceStyle},
                                    {"text", reference}});
    for (const auto &[i, credit] : {std::pair(0, leftCredit), std::pair(1, rightCredit)}) {
        if (credit.isEmpty())
            continue;
        elements.append(QJsonObject{{"type", "text"},
                                    {"frame", frame(QRectF(columnAt(i), creditTop, column, creditHeight))},
                                    {"style", creditStyle},
                                    {"text", credit},
                                    {"column", i}});
    }
    return elements;
}

bool relayoutSideBySide(SlideView &slide, const core::show::TextStyle &style, const QJsonObject &metadata)
{
    const QJsonArray &e = slide.elements;
    if (e.size() < 3 || metadata.value(QStringLiteral("secondTranslation")).toString().isEmpty())
        return false;
    for (const QJsonValue &v : e) {
        if (v.toObject().value("type").toString() != QLatin1String("text"))
            return false;
    }
    const auto text = [&](int i) { return e.at(i).toObject().value("text").toString(); };
    QString leftReference, rightReference, leftCredit, rightCredit;
    QStringList credits;
    if (e.size() == 3) {
        // The first layout: one footer, "John 3:16 (KJV · Yorùbá) · credit · credit".
        static const QRegularExpression footer(QStringLiteral(R"(^(.*) \((.+) · (.+?)\)(?: · (.*))?$)"));
        const auto m = footer.match(text(2));
        if (!m.hasMatch())
            return false;
        leftReference = QStringLiteral("%1 (%2)").arg(m.captured(1), m.captured(2));
        rightReference = QStringLiteral("%1 (%2)").arg(m.captured(1), m.captured(3));
        credits = m.captured(4).split(QStringLiteral(" · "), Qt::SkipEmptyParts);
    } else {
        leftReference = text(2);
        rightReference = text(3);
        for (int i = 4; i < e.size(); ++i) {
            const QJsonObject element = e.at(i).toObject();
            if (element.contains("column")) { // this layout: a credit per column
                (element.value("column").toInt() == 0 ? leftCredit : rightCredit) = element.value("text").toString();
            } else {
                credits << element.value("text").toString().split(QStringLiteral(" · "), Qt::SkipEmptyParts);
            }
        }
    }
    // One shared credit line: each part to the translation it names.
    const QString second = metadata.value(QStringLiteral("secondAbbreviation")).toString();
    const QString secondCopyright = metadata.value(QStringLiteral("secondCopyright")).toString();
    for (const QString &credit : credits) {
        const bool right = credits.size() == 2 ? &credit == &credits.back()
                                               : (!second.isEmpty() && credit.contains(second)) || credit == secondCopyright
                                                     || secondCopyright.startsWith(credit.section(u'.', 0, 0));
        (right ? rightCredit : leftCredit) = credit;
    }
    slide.elements = sideBySideElements(style, text(0), leftReference, leftCredit, text(1), rightReference, rightCredit);
    return true;
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

QString elementTypeName(ElementType type)
{
    switch (type) {
    case ElementType::Text: return QStringLiteral("text");
    case ElementType::Image: return QStringLiteral("image");
    case ElementType::Video: return QStringLiteral("video");
    case ElementType::Shape: return QStringLiteral("shape");
    case ElementType::Timer: return QStringLiteral("timer");
    case ElementType::Clock: return QStringLiteral("clock");
    }
    return {};
}

bool isComposed(const std::vector<Element> &elements)
{
    return std::any_of(elements.begin(), elements.end(), [](const Element &e) { return !e.style.isEmpty(); });
}

QJsonArray composedElements(Database &db, const std::vector<Element> &elements)
{
    std::vector<Element> ordered = elements;
    std::stable_sort(ordered.begin(), ordered.end(), [](const Element &a, const Element &b) { return a.zOrder < b.zOrder; });
    QJsonArray out;
    for (const Element &e : ordered) {
        QJsonObject json{{"type", elementTypeName(e.type)},
                         {"frame", QJsonArray{e.frame.x(), e.frame.y(), e.frame.width(), e.frame.height()}},
                         {"style", e.style}};
        if (e.type == ElementType::Text)
            json.insert("text", e.content);
        if (e.type == ElementType::Timer)
            json.insert("timerName", e.content);
        if ((e.type == ElementType::Image || e.type == ElementType::Video) && !e.mediaAssetId.isEmpty()) {
            if (auto asset = Repository<MediaAsset>(db).get(e.mediaAssetId); asset.ok())
                json.insert(e.type == ElementType::Image ? "image" : "video", QUrl::fromLocalFile(asset.value().filePath).toString());
        }
        out.append(json);
    }
    return out;
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

} // namespace lumen::app
