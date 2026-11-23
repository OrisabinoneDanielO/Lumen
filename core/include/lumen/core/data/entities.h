#pragma once

// Entities from the Data model in docs/design-doc.md.
//
// Conventions:
// - Every id is a UUID string, so libraries and plans can move between
//   machines without clashes. An empty id on create means "generate one".
// - An empty reference id (themeId, arrangementId, ...) means "none" and is
//   stored as NULL.
// - Ordered children carry a position; the repository does not renumber.
// - Settings whose shape changes often (styles, metadata, layouts) are JSON.

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTime>

#include <optional>

namespace lumen::core::data {

struct Library
{
    QString id{};
    QString name{};
    QString folderPath{};
    std::optional<QDateTime> lastBackup{}; // UTC

    bool operator==(const Library &) const = default;
};

enum class PresentationKind { Song, Scripture, Slides, Media };

struct Presentation
{
    QString id{};
    QString libraryId{};
    PresentationKind kind = PresentationKind::Song;
    QString title{};
    QString themeId{};      // optional
    QJsonObject metadata{}; // author, ccliNumber, copyright, key, tempo, ...

    bool operator==(const Presentation &) const = default;
};

// A labelled section of a presentation, e.g. "Verse 1" or "Chorus".
struct Group
{
    QString id{};
    QString presentationId{};
    int position = 0;
    QString label{};
    QString colour{}; // "#RRGGBB"
    QString hotkey{};

    bool operator==(const Group &) const = default;
};

// An order of groups, e.g. V1 C V2 C B C. Groups may repeat.
struct Arrangement
{
    QString id{};
    QString presentationId{};
    QString name{};
    bool isDefault = false; // at most one per presentation
    QStringList groupIds{};   // in play order

    bool operator==(const Arrangement &) const = default;
};

struct Slide
{
    QString id{};
    QString groupId{};
    int position = 0;
    QString notes{};
    QString chords{};
    QJsonObject transition{}; // empty: use the layer's transition

    bool operator==(const Slide &) const = default;
};

enum class ElementType { Text, Image, Video, Shape, Timer, Clock };

// A visual element on a slide, or a template element of a theme.
// Exactly one of slideId and themeId is set.
struct Element
{
    QString id{};
    QString slideId{};
    QString themeId{};
    ElementType type = ElementType::Text;
    QRectF frame{}; // x, y, width, height in slide units
    QJsonObject style{};
    int zOrder = 0;
    QString content{};      // text for text elements
    QString mediaAssetId{}; // image and video elements

    bool operator==(const Element &) const = default;
};

enum class ThemeTarget { Any, Song, Scripture, Slides, Media, LowerThird };

struct Theme
{
    QString id{};
    QString name{};
    ThemeTarget appliesTo = ThemeTarget::Any;
    QJsonObject background{};
    QRectF safeArea{}; // fractions of the output, e.g. (0.05, 0.05, 0.9, 0.9)
    bool isDefault = false; // the default for appliesTo (at most one each)

    bool operator==(const Theme &) const = default;
};

struct MediaAsset
{
    QString id{};
    QString libraryId{};
    QString filePath{};
    QString hash{}; // content hash, to find moved or duplicate files
    qint64 durationMs = 0;
    QString thumbnailPath{};
    bool loop = false;
    qint64 trimInMs = 0;
    qint64 trimOutMs = 0; // 0: play to the end

    bool operator==(const MediaAsset &) const = default;
};

struct Plan
{
    QString id{};
    QString name{};
    QDate date{}; // invalid: no date

    bool operator==(const Plan &) const = default;
};

// One row of a service plan: a presentation (songs, scripture, slides and
// media are all presentations) or a header with no presentation.
struct PlanItem
{
    QString id{};
    QString planId{};
    int position = 0;
    QString presentationId{}; // empty for a header item
    QString arrangementId{};  // empty: the presentation's default
    QString headerText{};
    QString notes{};

    bool operator==(const PlanItem &) const = default;
};

enum class OutputLayout { Audience, Stage, KeyFill };

struct Output
{
    QString id{};
    QString name{};   // shown to the operator, e.g. "Main"
    QString target{}; // screen or NDI target
    int width = 1920;
    int height = 1080;
    double frameRate = 60.0;
    OutputLayout layout = OutputLayout::Audience;
    QJsonObject layers{};    // layer filter and per-layer settings
    QString stageLayoutId{}; // for stage outputs

    bool operator==(const Output &) const = default;
};

struct StageLayout
{
    QString id{};
    QString name{};
    QJsonArray widgets{}; // current, next, clock, notes, timers, message

    bool operator==(const StageLayout &) const = default;
};

// A message template such as "Parent of {number}, please come to the nursery".
struct Message
{
    QString id{};
    QString name{};
    QString templateText{};
    QJsonArray fields{};

    bool operator==(const Message &) const = default;
};

// An overlay that stays up across slides (batch 14): a lower third (data
// {"line1", "line2"}) or a bug, a logo in a corner (data {"image", "corner",
// "size"}).
enum class PropKind { LowerThird, Bug };

struct Prop
{
    QString id{};
    QString name{};
    PropKind kind = PropKind::LowerThird;
    QJsonObject data{};

    bool operator==(const Prop &) const = default;
};

enum class TimerMode { Countdown, CountdownToTime, Elapsed };

struct Timer
{
    QString id{};
    QString name{};
    TimerMode mode = TimerMode::Countdown;
    qint64 durationMs = 0;            // Countdown
    std::optional<QTime> targetTime{};  // CountdownToTime, local time of day

    bool operator==(const Timer &) const = default;
};

} // namespace lumen::core::data
