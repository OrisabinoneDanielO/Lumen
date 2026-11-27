#include "scenemodel.h"

#include <QDateTime>
#include <QJsonObject>

#include <algorithm>

namespace lumen::render {

using core::show::layerCount;

// ---- ContentModel ---------------------------------------------------------

void ContentModel::set(const Content &content)
{
    if (content == m_content)
        return;
    m_content = content;
    emit changed();
}

QString ContentModel::kind() const
{
    switch (m_content.kind) {
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

QUrl ContentModel::image() const
{
    // An image, or a video's poster picture.
    return m_content.imagePath.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_content.imagePath);
}

QUrl ContentModel::video() const
{
    return m_content.kind == Content::Kind::Video ? QUrl::fromLocalFile(m_content.videoPath) : QUrl();
}

bool ContentModel::opaque() const
{
    return m_content.kind == Content::Kind::Colour && m_content.colour.alpha() == 255;
}

// ---- LayerModel -----------------------------------------------------------

LayerModel::LayerModel(int index, QObject *parent) : QObject(parent), m_index(index) {}

QString LayerModel::name() const
{
    return core::show::layerName(core::show::Layer(m_index));
}

void LayerModel::show(const Content &content, const Transition &transition, qint64 nowMs)
{
    if (content == m_current.content())
        return;
    // A change during a fade starts the new fade from what is now fully in.
    m_previous.set(m_current.content());
    m_current.set(content);
    m_startMs = nowMs;
    m_durationMs = transition.effectiveMs();
    m_transition = core::show::transitionKindName(transition.kind);
    if (m_durationMs == 0) {
        m_previous.set(Content::empty());
        setProgress(1.0);
    } else {
        setProgress(0.0);
    }
}

void LayerModel::update(qint64 nowMs)
{
    if (m_progress >= 1.0)
        return;
    const double progress =
        std::clamp(double(nowMs - m_startMs) / double(m_durationMs), 0.0, 1.0);
    if (progress >= 1.0)
        m_previous.set(Content::empty());
    setProgress(progress);
}

void LayerModel::setProgress(double progress)
{
    if (qFuzzyCompare(progress + 1.0, m_progress + 1.0))
        return;
    m_progress = progress;
    emit progressChanged();
}

// ---- Fader ----------------------------------------------------------------

bool Fader::setTarget(double target, int durationMs, qint64 nowMs)
{
    if (qFuzzyCompare(target + 1.0, m_target + 1.0))
        return false;
    m_from = m_level;
    m_target = target;
    m_startMs = nowMs;
    m_durationMs = durationMs;
    return update(nowMs);
}

bool Fader::update(qint64 nowMs)
{
    if (qFuzzyCompare(m_level + 1.0, m_target + 1.0))
        return false;
    // A partial fade back takes proportionally less time.
    const double distance = std::abs(m_target - m_from);
    const double total = double(m_durationMs) * distance;
    const double t = total <= 0.0 ? 1.0 : std::clamp(double(nowMs - m_startMs) / total, 0.0, 1.0);
    m_level = m_from + (m_target - m_from) * t;
    return true;
}

// ---- SceneModel -----------------------------------------------------------

SceneModel::SceneModel(QObject *parent) : QObject(parent)
{
    for (int i = 0; i < layerCount; ++i)
        m_layers.push_back(std::make_unique<LayerModel>(i));
    m_logo.set(m_scene.logoContent);
}

QList<QObject *> SceneModel::layers() const
{
    QList<QObject *> list;
    for (const auto &layer : m_layers)
        list << layer.get();
    return list;
}

void SceneModel::apply(const Scene &scene, qint64 nowMs)
{
    m_scene = scene;
    forgetUnusedVideoErrors();
    for (int i = 0; i < layerCount; ++i) {
        const auto &state = scene.layers[std::size_t(i)];
        auto &runner = m_cueRunners[std::size_t(i)];
        if (state.content.kind == Content::Kind::Cued) {
            // A backing track with cues: shown cue by cue (batch 20).
            if (!runner || !runner->sameRun(state.content.timeline))
                runner = std::make_unique<CueRunner>(state.content.timeline,
                                                     m_clock ? m_clock : std::function<qint64()>([nowMs] { return nowMs; }));
            int cue = -1;
            runner->update(nowMs, &cue);
            m_layers[std::size_t(i)]->show(runner->content(runner->cueAt(runner->positionMs(nowMs))), state.transition, nowMs);
            continue;
        }
        runner.reset();
        m_layers[std::size_t(i)]->show(state.content, state.transition, nowMs);
    }
    m_logo.set(scene.logoContent);
    const int overrideMs = scene.overrideTransition.effectiveMs();
    const bool logoMoved = m_logoFader.setTarget(scene.logo ? 1.0 : 0.0, overrideMs, nowMs);
    const bool blackMoved = m_blackFader.setTarget(scene.black ? 1.0 : 0.0, overrideMs, nowMs);
    if (logoMoved || blackMoved)
        emit levelsChanged();
}

void SceneModel::reportVideoError(const QString &path, const QString &message)
{
    const QUrl url(path);
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    if (local.isEmpty())
        return;
    qWarning("lumen-render: video %s could not be played: %s", qPrintable(local), qPrintable(message));
    m_videoErrors.insert(local, message.isEmpty() ? QStringLiteral("could not be played") : message);
}

void SceneModel::reportVideoPosition(const QString &path, qint64 positionMs, qint64 durationMs, bool playing)
{
    const QUrl url(path);
    m_videoPosition = {{"path", url.isLocalFile() ? url.toLocalFile() : path},
                       {"positionMs", positionMs},
                       {"durationMs", durationMs},
                       {"playing", playing}};
}

QJsonObject SceneModel::videoPosition() const
{
    // Only while that video is still on the Media layer.
    const Content &media = m_scene.layer(core::show::Layer::Media).content;
    if (media.kind != Content::Kind::Video || m_videoPosition.value("path").toString() != media.videoPath)
        return {};
    return m_videoPosition;
}

QJsonArray SceneModel::videoErrors() const
{
    QJsonArray list;
    for (auto it = m_videoErrors.cbegin(); it != m_videoErrors.cend(); ++it)
        list.append(QJsonObject{{QStringLiteral("path"), it.key()}, {QStringLiteral("message"), it.value()}});
    return list;
}

void SceneModel::forgetUnusedVideoErrors()
{
    if (m_videoErrors.isEmpty())
        return;
    QSet<QString> used;
    for (const auto &layer : m_scene.layers)
        used.insert(layer.content.videoPath);
    for (const QUrl &url : m_preloads)
        used.insert(url.toLocalFile());
    for (auto it = m_videoErrors.begin(); it != m_videoErrors.end();)
        it = used.contains(it.key()) ? std::next(it) : m_videoErrors.erase(it);
}

void SceneModel::preload(const QString &path)
{
    const QUrl url = QUrl::fromLocalFile(path);
    m_preloads.removeAll(url);
    m_preloads.append(url);
    while (m_preloads.size() > 2)
        m_preloads.removeFirst();
    emit preloadsChanged();
}

void SceneModel::setWallClock(qint64 wallMs, qint64 nowMs)
{
    m_wallBaseMs = wallMs - nowMs;
    update(nowMs);
}

void SceneModel::feedMidi(const QByteArray &bytes, qint64 nowMs)
{
    for (const auto &runner : m_cueRunners) {
        if (runner && runner->followsTimecode())
            runner->feedMidi(bytes, nowMs);
    }
}

QJsonObject SceneModel::timelineStatus(qint64 nowMs) const
{
    for (int i = 0; i < layerCount; ++i) {
        const auto &runner = m_cueRunners[std::size_t(i)];
        if (!runner)
            continue;
        const qint64 position = runner->positionMs(nowMs);
        return {{"layer", core::show::layerName(core::show::Layer(i))},
                {"positionMs", position},
                {"cue", runner->cueAt(position)},
                {"cues", runner->cueCount()},
                {"audio", runner->audioState()},
                {"audioPositionMs", runner->audioPositionMs()},
                {"worstSyncMs", runner->worstSyncMs()},
                {"audioStepMs", runner->audioStepMs()},
                {"switches", runner->switches()},
                {"timecode", runner->timecodeText(nowMs)}};
    }
    return {};
}

QString SceneModel::timerText(const QVariantMap &timer, qint64 wallMs) const
{
    return core::show::timerText(QJsonObject::fromVariantMap(timer), wallMs);
}

void SceneModel::update(qint64 nowMs)
{
    if (m_wallBaseMs < 0)
        m_wallBaseMs = QDateTime::currentMSecsSinceEpoch() - nowMs;
    const qint64 wall = (m_wallBaseMs + nowMs) / 100 * 100;
    if (wall != m_wallShownMs) {
        m_wallShownMs = wall;
        emit wallChanged();
    }
    // Cues come up on the first frame at or after their time, with a cut.
    for (int i = 0; i < layerCount; ++i) {
        int cue = -1;
        if (auto &runner = m_cueRunners[std::size_t(i)]; runner && runner->update(nowMs, &cue))
            m_layers[std::size_t(i)]->show(runner->content(cue), Transition::cut(), nowMs);
    }
    for (auto &layer : m_layers)
        layer->update(nowMs);
    const bool logoMoved = m_logoFader.update(nowMs);
    const bool blackMoved = m_blackFader.update(nowMs);
    if (logoMoved || blackMoved)
        emit levelsChanged();
}

} // namespace lumen::render
