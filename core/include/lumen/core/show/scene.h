#pragma once

// What is on air: the layer stack from the design doc ("Output and rendering
// pipeline"), plus the Logo and Black overrides. The show controller owns the
// scene; the render process draws it.

#include <QColor>
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

enum class TransitionKind { Cut, Fade };

struct Transition
{
    TransitionKind kind = TransitionKind::Cut;
    int durationMs = 0; // fades only

    static Transition cut() { return {}; }
    static Transition fade(int durationMs) { return {TransitionKind::Fade, durationMs}; }
    // Milliseconds the change takes on screen (0 for a cut).
    int effectiveMs() const { return kind == TransitionKind::Fade ? qMax(0, durationMs) : 0; }

    bool operator==(const Transition &) const = default;
};

// What one layer shows. Batch 4 supports a colour fill, text and a still
// image; video arrives in batch 9. Text may carry a theme style (batch 8,
// see textstyle.h) and a footer such as "Romans 8:28 (WEB)"; without a
// style the layer's default placement is used.
struct Content
{
    enum class Kind { Empty, Colour, Text, Image };

    Kind kind = Kind::Empty;
    QColor colour;     // fill for Colour, text colour for Text
    QString text;      // Text
    QString imagePath; // Image: a local file
    QJsonObject style; // Text: TextStyle JSON plus "size" (fitted pixel size)
    QString footer;    // Text: reference, copyright, ...

    static Content empty() { return {}; }
    static Content fill(const QColor &colour) { return {Kind::Colour, colour, {}, {}, {}, {}}; }
    static Content textContent(const QString &text, const QColor &colour = Qt::white)
    {
        return {Kind::Text, colour, text, {}, {}, {}};
    }
    static Content styledText(const QString &text, const QJsonObject &style, const QString &footer = {})
    {
        return {Kind::Text, Qt::white, text, {}, style, footer};
    }
    static Content image(const QString &path) { return {Kind::Image, {}, {}, path, {}, {}}; }

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

    LayerState &layer(Layer l) { return layers[std::size_t(l)]; }
    const LayerState &layer(Layer l) const { return layers[std::size_t(l)]; }

    bool operator==(const Scene &) const = default;
};

QJsonObject toJson(const Scene &scene);
std::optional<Scene> sceneFromJson(const QJsonObject &json);

} // namespace lumen::core::show
