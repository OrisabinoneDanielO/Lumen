#include "lumen/core/show/scene.h"

#include <QJsonArray>

#include <algorithm>

namespace lumen::core::show {

namespace {

constexpr std::array<const char *, layerCount> layerNames{
    "background", "media", "slide", "props", "messages", "masks"};

QString kindName(Content::Kind kind)
{
    switch (kind) {
    case Content::Kind::Colour: return QStringLiteral("colour");
    case Content::Kind::Text: return QStringLiteral("text");
    case Content::Kind::Image: return QStringLiteral("image");
    case Content::Kind::Video: return QStringLiteral("video");
    case Content::Kind::Timer: return QStringLiteral("timer");
    case Content::Kind::Prop: return QStringLiteral("prop");
    case Content::Kind::Composed: return QStringLiteral("composed");
    case Content::Kind::Cued: return QStringLiteral("cued");
    case Content::Kind::Empty: break;
    }
    return QStringLiteral("empty");
}

std::optional<Content::Kind> kindFromName(const QString &name)
{
    if (name == QLatin1String("empty"))
        return Content::Kind::Empty;
    if (name == QLatin1String("colour"))
        return Content::Kind::Colour;
    if (name == QLatin1String("text"))
        return Content::Kind::Text;
    if (name == QLatin1String("image"))
        return Content::Kind::Image;
    if (name == QLatin1String("video"))
        return Content::Kind::Video;
    if (name == QLatin1String("timer"))
        return Content::Kind::Timer;
    if (name == QLatin1String("prop"))
        return Content::Kind::Prop;
    if (name == QLatin1String("composed"))
        return Content::Kind::Composed;
    if (name == QLatin1String("cued"))
        return Content::Kind::Cued;
    return std::nullopt;
}

QJsonObject contentJson(const Content &content)
{
    QJsonObject json{{QStringLiteral("kind"), kindName(content.kind)}};
    if (content.kind == Content::Kind::Colour || content.kind == Content::Kind::Text)
        json.insert(QStringLiteral("colour"), content.colour.name(QColor::HexArgb));
    if (content.kind == Content::Kind::Text) {
        json.insert(QStringLiteral("text"), content.text);
        if (!content.style.isEmpty())
            json.insert(QStringLiteral("style"), content.style);
        if (!content.footer.isEmpty())
            json.insert(QStringLiteral("footer"), content.footer);
    }
    if (content.kind == Content::Kind::Image)
        json.insert(QStringLiteral("image"), content.imagePath);
    if (content.kind == Content::Kind::Prop)
        json.insert(QStringLiteral("prop"), content.prop);
    if (content.kind == Content::Kind::Composed)
        json.insert(QStringLiteral("elements"), content.elements);
    if (content.kind == Content::Kind::Cued)
        json.insert(QStringLiteral("timeline"), content.timeline);
    if (content.kind == Content::Kind::Timer) {
        json.insert(QStringLiteral("timer"), content.timer);
        if (!content.text.isEmpty())
            json.insert(QStringLiteral("text"), content.text);
        if (!content.style.isEmpty())
            json.insert(QStringLiteral("style"), content.style);
    }
    if (content.kind == Content::Kind::Video) {
        json.insert(QStringLiteral("video"), content.videoPath);
        json.insert(QStringLiteral("loop"), content.loop);
        json.insert(QStringLiteral("volume"), content.volume);
        if (!content.imagePath.isEmpty())
            json.insert(QStringLiteral("poster"), content.imagePath);
    }
    return json;
}

std::optional<Content> contentFrom(const QJsonValue &value)
{
    const QJsonObject json = value.toObject();
    const auto kind = kindFromName(json.value(QStringLiteral("kind")).toString(QStringLiteral("empty")));
    if (!kind)
        return std::nullopt;
    Content content;
    content.kind = *kind;
    if (*kind == Content::Kind::Colour || *kind == Content::Kind::Text) {
        content.colour = QColor::fromString(json.value(QStringLiteral("colour")).toString());
        if (!content.colour.isValid())
            return std::nullopt;
    }
    if (*kind == Content::Kind::Text) {
        content.text = json.value(QStringLiteral("text")).toString();
        content.style = json.value(QStringLiteral("style")).toObject();
        content.footer = json.value(QStringLiteral("footer")).toString();
    }
    if (*kind == Content::Kind::Image)
        content.imagePath = json.value(QStringLiteral("image")).toString();
    if (*kind == Content::Kind::Cued) {
        content.colour = Qt::white;
        content.timeline = json.value(QStringLiteral("timeline")).toObject();
        // Every cue must be readable content (not cued itself).
        for (const QJsonValue &cue : content.timeline.value(QStringLiteral("cues")).toArray()) {
            const auto inner = contentFrom(cue.toObject().value(QStringLiteral("content")));
            if (!inner || inner->kind == Content::Kind::Cued)
                return std::nullopt;
        }
    }
    if (*kind == Content::Kind::Composed) {
        content.colour = Qt::white;
        content.elements = json.value(QStringLiteral("elements")).toArray();
    }
    if (*kind == Content::Kind::Prop) {
        content.colour = Qt::white;
        content.prop = json.value(QStringLiteral("prop")).toObject();
        if (!content.prop.contains(QStringLiteral("lowerThird")) && !content.prop.contains(QStringLiteral("bug")))
            return std::nullopt;
    }
    if (*kind == Content::Kind::Timer) {
        content.colour = Qt::white;
        content.timer = json.value(QStringLiteral("timer")).toObject();
        content.text = json.value(QStringLiteral("text")).toString();
        content.style = json.value(QStringLiteral("style")).toObject();
        const QString mode = content.timer.value(QStringLiteral("mode")).toString();
        if (mode != QLatin1String("countdown") && mode != QLatin1String("countdownToTime") && mode != QLatin1String("elapsed"))
            return std::nullopt;
    }
    if (*kind == Content::Kind::Video) {
        content.videoPath = json.value(QStringLiteral("video")).toString();
        content.loop = json.value(QStringLiteral("loop")).toBool();
        content.volume = qBound(0.0, json.value(QStringLiteral("volume")).toDouble(1.0), 1.0);
        content.imagePath = json.value(QStringLiteral("poster")).toString();
        if (content.videoPath.isEmpty())
            return std::nullopt;
    }
    return content;
}

QJsonObject transitionJson(const Transition &transition)
{
    if (transition.kind == TransitionKind::Cut)
        return {{QStringLiteral("kind"), QStringLiteral("cut")}};
    return {{QStringLiteral("kind"), QStringLiteral("fade")},
            {QStringLiteral("ms"), transition.durationMs}};
}

std::optional<Transition> transitionFrom(const QJsonValue &value)
{
    const QJsonObject json = value.toObject();
    const QString kind = json.value(QStringLiteral("kind")).toString(QStringLiteral("cut"));
    if (kind == QLatin1String("cut"))
        return Transition::cut();
    if (kind == QLatin1String("fade"))
        return Transition::fade(qMax(0, json.value(QStringLiteral("ms")).toInt()));
    return std::nullopt;
}

} // namespace

QString layerName(Layer layer)
{
    return QLatin1String(layerNames[std::size_t(layer)]);
}

std::optional<Layer> layerFromName(const QString &name)
{
    for (std::size_t i = 0; i < layerNames.size(); ++i) {
        if (name == QLatin1String(layerNames[i]))
            return Layer(i);
    }
    return std::nullopt;
}

QJsonObject toJson(const Scene &scene)
{
    QJsonArray layers;
    for (const LayerState &state : scene.layers) {
        layers.append(QJsonObject{{QStringLiteral("content"), contentJson(state.content)},
                                  {QStringLiteral("transition"), transitionJson(state.transition)}});
    }
    return {{QStringLiteral("sequence"), qint64(scene.sequence)},
            {QStringLiteral("layers"), layers},
            {QStringLiteral("logo"), scene.logo},
            {QStringLiteral("black"), scene.black},
            {QStringLiteral("logoContent"), contentJson(scene.logoContent)},
            {QStringLiteral("overrideTransition"), transitionJson(scene.overrideTransition)}};
}

std::optional<Scene> sceneFromJson(const QJsonObject &json)
{
    const QJsonArray layers = json.value(QStringLiteral("layers")).toArray();
    if (layers.size() != layerCount)
        return std::nullopt;

    Scene scene;
    const qint64 sequence = json.value(QStringLiteral("sequence")).toInteger(-1);
    if (sequence < 0)
        return std::nullopt;
    scene.sequence = quint64(sequence);

    for (int i = 0; i < layerCount; ++i) {
        const QJsonObject layer = layers.at(i).toObject();
        const auto content = contentFrom(layer.value(QStringLiteral("content")));
        const auto transition = transitionFrom(layer.value(QStringLiteral("transition")));
        if (!content || !transition)
            return std::nullopt;
        scene.layers[std::size_t(i)] = {*content, *transition};
    }

    scene.logo = json.value(QStringLiteral("logo")).toBool();
    scene.black = json.value(QStringLiteral("black")).toBool();
    if (json.contains(QStringLiteral("logoContent"))) {
        const auto logo = contentFrom(json.value(QStringLiteral("logoContent")));
        if (!logo)
            return std::nullopt;
        scene.logoContent = *logo;
    }
    if (json.contains(QStringLiteral("overrideTransition"))) {
        const auto transition = transitionFrom(json.value(QStringLiteral("overrideTransition")));
        if (!transition)
            return std::nullopt;
        scene.overrideTransition = *transition;
    }
    return scene;
}

qint64 timerValueMs(const QJsonObject &timer, qint64 nowMs)
{
    if (timer.contains(QStringLiteral("pausedMs")))
        return timer.value(QStringLiteral("pausedMs")).toInteger();
    if (timer.value(QStringLiteral("mode")).toString() == QLatin1String("elapsed"))
        return nowMs - timer.value(QStringLiteral("startedAtMs")).toInteger(nowMs);
    return timer.value(QStringLiteral("endsAtMs")).toInteger(nowMs) - nowMs;
}

QString timerText(const QJsonObject &timer, qint64 nowMs)
{
    const bool countdown = timer.value(QStringLiteral("mode")).toString() != QLatin1String("elapsed");
    qint64 ms = std::max<qint64>(0, timerValueMs(timer, nowMs));
    // Counting down, a second shows until it has fully passed (10.3 s left
    // shows 0:11); counting up, once it has (10.3 s shows 0:10).
    const qint64 seconds = countdown ? (ms + 999) / 1000 : ms / 1000;
    const qint64 h = seconds / 3600, m = (seconds / 60) % 60, s = seconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QJsonObject contentToJson(const Content &content)
{
    return contentJson(content);
}

std::optional<Content> contentFromJson(const QJsonValue &json)
{
    return contentFrom(json);
}

} // namespace lumen::core::show
