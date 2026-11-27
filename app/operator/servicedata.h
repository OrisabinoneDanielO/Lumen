#pragma once

// Reads a service plan from the library database into what the operator
// window shows: plan rows, each item's slides in arrangement order, and the
// song list.

#include "lumen/core/data/database.h"
#include "lumen/core/data/entities.h"
#include "lumen/core/result.h"
#include "lumen/core/show/textstyle.h"

#include <QColor>
#include <QDate>
#include <QJsonArray>
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
    // A slide made in the editor (batch 16): its elements, positioned, as
    // composed content (see scene.h; timers carry "timerName" until they go
    // live). Empty for every other slide.
    QJsonArray elements;
    // A video: loop until cleared, or play once and stop (the presentation's
    // "videoLoop"; once by default). Last, so positional initialisers stay
    // as they were.
    bool mediaLoop = false;
    // A video's start and end points (the presentation's "trimInMs" and
    // "trimOutMs"; 0: the start, the end).
    qint64 trimInMs = 0;
    qint64 trimOutMs = 0;
    // A song slide's lines with inline chords ("[G]Amazing [C]grace"), for
    // the stage display; empty if none.
    QString chords{};
};

// The JSON for composed content from a slide's stored elements, bottom
// first. Pictures and videos become file:// URLs.
QJsonArray composedElements(core::data::Database &db, const std::vector<core::data::Element> &elements);
// True if a slide is drawn as composed content: one of its elements was
// styled in the editor.
bool isComposed(const std::vector<core::data::Element> &elements);
QString elementTypeName(core::data::ElementType type);

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
    int transpose = 0;      // semitones the band plays it up (+) or down (-)
    int autoAdvance = 0;    // seconds per slide when live (looping); 0: by hand
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

// Scripture: where the reference goes. "below" or "above" the text, or
// "lowerThird": the text and its reference in the bottom third of the
// screen, leaving the picture (a camera, a stream) above it.
core::show::TextStyle scriptureStyle(core::show::TextStyle style, const QString &position);
// Two translations side by side on one slide, as composed content: a column
// each, and under each passage its own reference ("John 3:16 (KJV)") and its
// own copyright line (none if empty), placed as one block where the theme
// puts a slide's text. Elements: left text, right text, left reference,
// right reference, then the left and right credits (each only if any).
QJsonArray sideBySideElements(const core::show::TextStyle &style, const QString &left, const QString &leftReference,
                              const QString &leftCredit, const QString &right, const QString &rightReference,
                              const QString &rightCredit);
// A side by side slide stored earlier (an older layout), laid out again
// with the current one and the item's theme. metadata: the scripture
// presentation's (credits, abbreviations). False if it is not one.
bool relayoutSideBySide(SlideView &slide, const core::show::TextStyle &style, const QJsonObject &metadata);

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

} // namespace lumen::app
