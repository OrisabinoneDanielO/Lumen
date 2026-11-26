#pragma once

// Reads a service plan from the library database into what the operator
// window shows: plan rows, each item's slides in arrangement order, and the
// song list.

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"
#include "lumen/core/show/textstyle.h"

#include <QColor>
#include <QDate>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace lumen::app {

struct SlideView
{
    QString slideId;
    QString group;  // "Verse 1", "Chorus", ...
    QColor colour;  // the group's colour
    QString text;   // text elements, top to bottom
    QString footer; // e.g. "Amazing Grace · John Newton · Public domain"
    int fontSize = 0; // fitted pixel size (1080p) when the item has a theme
    // A media slide (batch 9): a video or picture instead of text.
    QString mediaKind; // "video", "image" or empty
    QString mediaPath;
    QString thumbnail;
    // A countdown slide (batch 12): shows a timer under its text. timerName
    // picks the timer; empty means the first countdown to a time of day.
    bool hasTimer = false;
    QString timerName;
};

struct ArrangementChip
{
    QString label; // "V1", "C", "Tag", ...
    QColor background;
    QColor foreground;
};

struct ItemView
{
    QString planItemId;     // empty for a song shown from the library
    QString notes;          // the plan item's notes
    QString key;            // a song's key ("G"), for the stage display
    QString presentationId; // empty for a header
    QString kind;     // "SONG", "BIBLE", "SLIDES", "MEDIA", "TIMER"; empty for a header
    QString title;
    QString subtitle; // under the title in the plan
    QString detail;   // above the slide grid: author · copyright · theme
    std::vector<SlideView> slides; // in play order (arrangement expanded)
    std::vector<ArrangementChip> chips;
    std::optional<core::show::TextStyle> style; // from the item's theme
    QJsonObject background;                     // from the item's theme
};

// Gives an item a theme's style and background, and fits each slide's text
// (batch 8). Slides keep their own footers.
void applyTheme(ItemView &item, const core::show::TextStyle &style, const QJsonObject &background);

struct PlanView
{
    QString id;
    QString name;
    QDate date;
    std::vector<ItemView> items;
};

struct SongRow
{
    QString title;
    QString author;
    QString copyright;
    QString key;
    QString presentationId;
};

// Colour for a group with none set: verses blue, choruses red, bridges
// purple (design doc), others from the same family.
QColor defaultGroupColour(const QString &label);

// "Verse 1" -> "V1", "Chorus" -> "C", "Pre-chorus" -> "PC", "Tag" -> "Tag".
QString groupAbbreviation(const QString &label);

core::Result<PlanView> loadPlan(core::data::Database &db, const QString &planId);

// One presentation as a plan row would show it, in the given arrangement (or
// its default). Used for songs shown straight from the library.
core::Result<ItemView> loadPresentationView(core::data::Database &db, const QString &presentationId,
                                            const QString &arrangementId = {});

// The plan with the latest date, or nothing if there are no plans.
core::Result<std::optional<PlanView>> loadLatestPlan(core::data::Database &db);

core::Result<std::vector<SongRow>> loadSongs(core::data::Database &db);

// Fills a database with the sample service from design/mockups/Main.html
// (public-domain hymns; scripture from the World English Bible). Used only by
// --design-preview, on an in-memory database.
core::Status seedSampleService(core::data::Database &db);

} // namespace lumen::app
