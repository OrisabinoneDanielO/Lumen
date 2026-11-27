#pragma once

// What is on air: the layer stack from the design doc ("Output and rendering
// pipeline"), plus the Logo and Black overrides. The show controller owns the
// scene; the render process draws it.

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <array>
#include <optional>

namespace lumen::core::show {

// Bottom to top.
enum class Layer { Background, Media, Slide, Props, Messages, Masks };
inline constexpr int layerCount = 6;

QString layerName(Layer layer);              // "background", "media", ...
std::optional<Layer> layerFromName(const QString &name);

// How a layer changes to new content: at once (Cut), or over durationMs by
// fading (Fade), sliding the new content in from the right (Push) or from
// below (PushUp), uncovering it from left to right (Wipe), or growing it in
// while the old fades (Zoom).
enum class TransitionKind { Cut, Fade, Push, PushUp, Wipe, Zoom };

QString transitionKindName(TransitionKind kind);           // "cut", "fade", "push", "pushUp", "wipe", "zoom"
std::optional<TransitionKind> transitionKindFromName(const QString &name);

struct Transition
{
    TransitionKind kind = TransitionKind::Cut;
    int durationMs = 0; // all but cuts

    static Transition cut() { return {}; }
    static Transition fade(int durationMs) { return {TransitionKind::Fade, durationMs}; }
    static Transition of(TransitionKind kind, int durationMs) { return {kind, durationMs}; }
    // Milliseconds the change takes on screen (0 for a cut).
    int effectiveMs() const { return kind == TransitionKind::Cut ? 0 : qMax(0, durationMs); }

    bool operator==(const Transition &) const = default;
};

// What one layer shows: a colour fill, text, a still image, video (batch 9;
// decoded and played by the render process itself), or a timer (batch 12;
// counted by the render process from its own clock, so it keeps time even
// if the operator UI stops). Text may carry
// a theme style (batch 8, see textstyle.h) and a footer such as "Romans 8:28
// (WEB)"; without a style the layer's default placement is used.
struct Content
{
    enum class Kind { Empty, Colour, Text, Image, Video, Timer, Prop, Composed, Cued };

    Kind kind = Kind::Empty;
    QColor colour;     // fill for Colour, text colour for Text
    QString text;      // Text
    QString imagePath; // Image: a local file; Video: a poster picture (optional)
    QJsonObject style; // Text: TextStyle JSON plus "size" (fitted pixel size)
    QString footer;    // Text: reference, copyright, ...
    QString videoPath; // Video: a local file
    bool loop = false; // Video: start again at the end (motion backgrounds)
    double volume = 1.0; // Video: 0 silent .. 1 full
    // Timer: {"mode": "countdown" | "countdownToTime" | "elapsed",
    //         "endsAtMs" (countdowns) or "startedAtMs" (elapsed): wall-clock
    //         milliseconds since 1970 UTC, "pausedMs": the value shown while
    //         paused (absent when running)}. `text` is a label shown with it.
    QJsonObject timer;
    // Prop (batch 14, the Props layer): {"lowerThird": {"id", "line1",
    // "line2"}, "bug": {"id", "image" (a file:// URL), "corner" (topLeft,
    // topRight, bottomLeft, bottomRight), "size" (fraction of the height)}},
    // either or both.
    QJsonObject prop;
    // Composed (batch 16, slides made in the editor): positioned elements on
    // a 1920 x 1080 grid, bottom first. Each {"type": "text" | "shape" |
    // "image" | "video" | "clock" | "timer", "frame": [x, y, w, h], "style":
    // {...}, "text", "image" / "video" (file:// URLs), "timer" (a timer
    // spec, see above)}.
    QJsonArray elements;
    // Cued (batch 20): slides that follow a backing track. The render
    // process plays the track and shows, every frame, the cue whose time has
    // come: {"id" (changes when started again), "audio" (a local file, or
    // none: the renderer's clock), "playing", "offsetMs" (where playing
    // starts, or where it is paused), "cues": [{"ms", "content": a content's
    // JSON}] in time order}.
    QJsonObject timeline;

    static Content empty() { return {}; }
    static Content fill(const QColor &colour) { return {Kind::Colour, colour, {}, {}, {}, {}, {}, false, 1.0, {}, {}, {}, {}}; }
    static Content textContent(const QString &text, const QColor &colour = Qt::white)
    {
        return {Kind::Text, colour, text, {}, {}, {}, {}, false, 1.0, {}, {}, {}, {}};
    }
    static Content styledText(const QString &text, const QJsonObject &style, const QString &footer = {})
    {
        return {Kind::Text, Qt::white, text, {}, style, footer, {}, false, 1.0, {}, {}, {}, {}};
    }
    static Content image(const QString &path) { return {Kind::Image, {}, {}, path, {}, {}, {}, false, 1.0, {}, {}, {}, {}}; }
    static Content video(const QString &path, bool loop, double volume, const QString &poster = {})
    {
        return {Kind::Video, {}, {}, poster, {}, {}, path, loop, volume, {}, {}, {}, {}};
    }

    static Content timerContent(const QJsonObject &timer, const QString &label = {})
    {
        Content c{Kind::Timer, Qt::white, label, {}, {}, {}, {}, false, 1.0, {}, {}, {}, {}};
        c.timer = timer;
        return c;
    }

    static Content composed(const QJsonArray &elements)
    {
        Content c{Kind::Composed, Qt::white, {}, {}, {}, {}, {}, false, 1.0, {}, {}, {}, {}};
        c.elements = elements;
        return c;
    }

    static Content cued(const QJsonObject &timeline)
    {
        Content c{Kind::Cued, Qt::white, {}, {}, {}, {}, {}, false, 1.0, {}, {}, {}, {}};
        c.timeline = timeline;
        return c;
    }

    static Content propContent(const QJsonObject &prop)
    {
        Content c{Kind::Prop, Qt::white, {}, {}, {}, {}, {}, false, 1.0, {}, {}, {}, {}};
        c.prop = prop;
        return c;
    }

    // Video: play from trimInMs and stop (or loop back) at trimOutMs (0: the
    // end). Last, so positional initialisers stay as they were.
    qint64 trimInMs = 0;
    qint64 trimOutMs = 0;

    bool isEmpty() const { return kind == Kind::Empty; }
    bool operator==(const Content &) const = default;
};

struct LayerState
{
    Content content;
    Transition transition; // used when this layer's content changes

    bool operator==(const LayerState &) const = default;
};

struct Scene
{
    // Increases with every change, so a renderer can ignore stale snapshots.
    quint64 sequence = 0;
    std::array<LayerState, layerCount> layers{};

    // Overrides above every layer. Black is above Logo.
    bool logo = false;
    bool black = false;
    Content logoContent = Content::textContent(QStringLiteral("Lumen Presenter"));
    Transition overrideTransition = Transition::fade(500);

    // Sound playing beside the slides (background music, a track): a Video
    // content of an audio file, or empty. Only the audience output plays it;
    // the layers stay as they are.
    Content audio;

    LayerState &layer(Layer l) { return layers[std::size_t(l)]; }
    const LayerState &layer(Layer l) const { return layers[std::size_t(l)]; }

    bool operator==(const Scene &) const = default;
};

// What a timer shows at wall-clock time nowMs (ms since 1970 UTC): "4:59",
// "1:02:03"; countdowns stop at "0:00". Rounds up while counting down, so
// "1:00" shows for the whole last minute's first second, as clocks do.
QString timerText(const QJsonObject &timer, qint64 nowMs);
// Milliseconds the timer shows (negative past the end of a countdown).
qint64 timerValueMs(const QJsonObject &timer, qint64 nowMs);

QJsonObject toJson(const Scene &scene);
// One content as JSON and back (cues carry their slides this way).
QJsonObject contentToJson(const Content &content);
std::optional<Content> contentFromJson(const QJsonValue &json);
std::optional<Scene> sceneFromJson(const QJsonObject &json);

} // namespace lumen::core::show
