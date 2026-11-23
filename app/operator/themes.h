#pragma once

// Themes as the operator UI uses them. A theme (data model) is a background,
// a safe area and element templates; its body-text template holds the
// TextStyle. Each kind of item has a default theme.

#include "lumen/core/data/database.h"
#include "lumen/core/data/entities.h"
#include "lumen/core/result.h"
#include "lumen/core/show/scene.h"
#include "lumen/core/show/textstyle.h"

#include <QJsonObject>

#include <optional>
#include <vector>

namespace lumen::app {

struct ThemeView
{
    QString id;
    QString name;
    core::data::ThemeTarget appliesTo = core::data::ThemeTarget::Song;
    bool isDefault = false;
    QJsonObject background; // {"colour": "#RRGGBB"}, {"image": path} or {} (none)
    core::show::TextStyle style;
};

// The built-in look for each kind: songs centred on navy, scripture left
// aligned in a serif face with a gold reference footer, lower thirds at the
// bottom over the picture.
ThemeView builtInTheme(core::data::ThemeTarget target);

// Makes sure songs, scripture and lower thirds each have a default theme,
// adopting an existing theme of that kind if there is one.
core::Status ensureDefaultThemes(core::data::Database &db);

core::Result<std::vector<ThemeView>> listThemes(core::data::Database &db);
core::Result<ThemeView> loadTheme(core::data::Database &db, const QString &id);

// The presentation's own theme, else the default for its kind (slides and
// media use the song default). Nothing if no theme applies.
std::optional<ThemeView> themeFor(core::data::Database &db, const core::data::Presentation &presentation);
std::optional<ThemeView> defaultTheme(core::data::Database &db, core::data::ThemeTarget target);

// Saves name, background, safe area and style; creates the theme if it has no
// id yet. Making it the default unsets the previous default for its kind.
core::Result<ThemeView> saveTheme(core::data::Database &db, ThemeView theme);

// The Background layer content for a theme background (empty: no change).
std::optional<core::show::Content> backgroundContent(const QJsonObject &background);

// A theme's style as sent with text: the style plus the fitted size.
QJsonObject styleWithSize(const core::show::TextStyle &style, int pixelSize);

} // namespace lumen::app
