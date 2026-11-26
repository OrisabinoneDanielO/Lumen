#include "lumen/core/show/scene.h"

#include <QJsonArray>

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

} // namespace lumen::core::show
