#include "themes.h"

#include "lumen/core/data/repository.h"

#include <algorithm>

namespace lumen::app {

using namespace core;
using namespace core::data;
using core::show::TextStyle;

namespace {

const QString bodyTemplate = QStringLiteral("{body}");

// The theme's body-text template element, if it has one.
std::optional<Element> bodyElement(Database &db, const QString &themeId)
{
    auto elements = Repository<Element>(db).listChildren(themeId);
    if (!elements.ok())
        return std::nullopt;
    for (const Element &element : elements.value()) {
        if (element.type == ElementType::Text && element.content == bodyTemplate)
            return element;
    }
    return std::nullopt;
}

ThemeView viewOf(Database &db, const Theme &theme)
{
    ThemeView view{theme.id, theme.name, theme.appliesTo, theme.isDefault, theme.background, {}};
    if (const auto body = bodyElement(db, theme.id))
        view.style = TextStyle::fromJson(body->style);
    else
        view.style = builtInTheme(theme.appliesTo).style;
    if (theme.safeArea.width() > 0.1 && theme.safeArea.height() > 0.1)
        view.style.safeArea = theme.safeArea;
    return view;
}

ThemeTarget targetFor(PresentationKind kind)
{
    switch (kind) {
    case PresentationKind::Scripture: return ThemeTarget::Scripture;
    case PresentationKind::Song:
    case PresentationKind::Slides:
    case PresentationKind::Media: break;
    }
    return ThemeTarget::Song;
}

} // namespace

ThemeView builtInTheme(ThemeTarget target)
{
    ThemeView view;
    view.appliesTo = target;
    TextStyle &s = view.style;
    switch (target) {
    case ThemeTarget::Scripture:
        view.name = QStringLiteral("Scripture");
        view.background = {{"colour", "#1B1F2B"}};
        s.font = QStringLiteral("Noto Serif");
        s.weight = 400;
        s.maxSize = 64;
        s.minSize = 36;
        s.align = QStringLiteral("left");
        s.lineHeight = 1.3;
        s.footerSize = 30;
        s.footerColour = QColor(0xF2, 0xC3, 0x8B);
        break;
    case ThemeTarget::LowerThird:
        view.name = QStringLiteral("Lower third");
        view.background = {};
        s.maxSize = 48;
        s.minSize = 28;
        s.align = QStringLiteral("left");
        s.valign = QStringLiteral("bottom");
        s.outline = 2;
        s.safeArea = QRectF(0.05, 0.68, 0.9, 0.27);
        s.footerPosition = QStringLiteral("none");
        break;
    default:
        view.name = QStringLiteral("Lyrics, centred");
        view.background = {{"colour", "#14213D"}};
        s.footerSize = 24;
        s.footerColour = QColor(0xC9, 0xCE, 0xD6);
        break;
    }
    return view;
}

Status ensureDefaultThemes(Database &db)
{
    return db.inTransaction([&]() -> Status {
        auto themes = Repository<Theme>(db).list();
        if (!themes.ok())
            return themes.error();
        for (const ThemeTarget target : {ThemeTarget::Song, ThemeTarget::Scripture, ThemeTarget::LowerThird}) {
            const auto &list = themes.value();
            auto chosen = std::find_if(list.begin(), list.end(), [&](const Theme &t) {
                return t.appliesTo == target && t.isDefault;
            });
            if (chosen == list.end()) {
                chosen = std::find_if(list.begin(), list.end(),
                                      [&](const Theme &t) { return t.appliesTo == target; });
            }
            ThemeView view = builtInTheme(target);
            view.isDefault = true;
            if (chosen != list.end()) {
                // Adopt the existing theme; give it what it lacks.
                if (chosen->isDefault && bodyElement(db, chosen->id))
                    continue;
                view.id = chosen->id;
                view.name = chosen->name;
                if (!chosen->background.isEmpty())
                    view.background = chosen->background;
                if (const auto body = bodyElement(db, chosen->id))
                    view.style = TextStyle::fromJson(body->style);
            }
            if (auto saved = saveTheme(db, view); !saved.ok())
                return saved.error();
        }
        return success();
    });
}

Result<std::vector<ThemeView>> listThemes(Database &db)
{
    auto themes = Repository<Theme>(db).list();
    if (!themes.ok())
        return themes.error();
    std::vector<ThemeView> views;
    for (const Theme &theme : themes.value())
        views.push_back(viewOf(db, theme));
    return views;
}

Result<ThemeView> loadTheme(Database &db, const QString &id)
{
    auto theme = Repository<Theme>(db).get(id);
    if (!theme.ok())
        return theme.error();
    return viewOf(db, theme.value());
}

std::optional<ThemeView> defaultTheme(Database &db, ThemeTarget target)
{
    auto themes = Repository<Theme>(db).list();
    if (!themes.ok())
        return std::nullopt;
    for (const Theme &theme : themes.value()) {
        if (theme.appliesTo == target && theme.isDefault)
            return viewOf(db, theme);
    }
    return std::nullopt;
}

std::optional<ThemeView> themeFor(Database &db, const Presentation &presentation)
{
    if (!presentation.themeId.isEmpty()) {
        if (auto own = loadTheme(db, presentation.themeId); own.ok())
            return own.value();
    }
    return defaultTheme(db, targetFor(presentation.kind));
}

Result<ThemeView> saveTheme(Database &db, ThemeView view)
{
    Status status = db.inTransaction([&]() -> Status {
        Repository<Theme> themes(db);
        if (view.isDefault) {
            // Only one default per kind.
            auto all = themes.list();
            if (!all.ok())
                return all.error();
            for (Theme other : all.value()) {
                if (other.id != view.id && other.appliesTo == view.appliesTo && other.isDefault) {
                    other.isDefault = false;
                    if (auto s = themes.update(other); !s.ok())
                        return s;
                }
            }
        }
        const Theme theme{view.id, view.name, view.appliesTo, view.background, view.style.safeArea, view.isDefault};
        if (view.id.isEmpty()) {
            auto created = themes.create(theme);
            if (!created.ok())
                return created.error();
            view.id = created.value().id;
        } else if (auto s = themes.update(theme); !s.ok()) {
            return s;
        }

        Repository<Element> elements(db);
        auto body = bodyElement(db, view.id);
        if (body) {
            body->style = view.style.toJson();
            return elements.update(*body);
        }
        auto created = elements.create({.themeId = view.id,
                                        .type = ElementType::Text,
                                        .frame = QRectF(0, 0, 1920, 1080),
                                        .style = view.style.toJson(),
                                        .content = bodyTemplate});
        if (!created.ok())
            return created.error();
        return success();
    });
    if (!status.ok())
        return status.error();
    return view;
}

std::optional<show::Content> backgroundContent(const QJsonObject &background)
{
    const QColor colour = QColor::fromString(background.value("colour").toString());
    if (colour.isValid())
        return show::Content::fill(colour);
    const QString image = background.value("image").toString();
    if (!image.isEmpty())
        return show::Content::image(image);
    return std::nullopt;
}

QJsonObject styleWithSize(const TextStyle &style, int pixelSize)
{
    QJsonObject json = style.toJson();
    json.insert(QStringLiteral("size"), pixelSize);
    return json;
}

} // namespace lumen::app
