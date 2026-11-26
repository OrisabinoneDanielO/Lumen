#include "cuecontroller.h"

#include "medialibrary.h"
#include "operatorcontroller.h"

#include "lumen/core/data/repository.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QJsonArray>
#include <QMediaPlayer>

namespace lumen::app {

using namespace core::data;

CueController::CueController(OperatorController &op, QObject *parent) : QObject(parent), m_op(op)
{
    connect(&m_op, &OperatorController::stateChanged, this, &CueController::follow);
    connect(&m_op, &OperatorController::slideSentLive, this, &CueController::onSlideSent);
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &CueController::ticked);
}

CueController::~CueController() = default;

void CueController::attach(Database &db)
{
    m_db = &db;
    follow();
}

QString CueController::timeText(qint64 ms)
{
    ms = std::max<qint64>(0, ms);
    return QStringLiteral("%1:%2.%3")
        .arg(ms / 60000)
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms % 1000) / 10, 2, 10, QLatin1Char('0'));
}

QString CueController::trackName() const
{
    return m_trackPath.isEmpty() ? QString() : QFileInfo(m_trackPath).fileName();
}

bool CueController::playing() const
{
    return m_op.cuesPlaying();
}

QString CueController::position() const
{
    return m_recording ? timeText(recordingPosition()) : QString();
}

// Keeps to the item selected in the operator window.
void CueController::follow()
{
    const ItemView *item = m_op.selectedItem();
    const QString id = item ? item->presentationId : QString();
    if (id == m_presentationId && !id.isEmpty()) {
        refresh(); // its slides may have changed
        return;
    }
    if (m_recording)
        stopRecording();
    m_presentationId = id;
    m_title = item ? item->title : QString();
    m_timeline = {};
    m_trackPath.clear();
    m_message.clear();
    if (m_db && !id.isEmpty()) {
        if (auto p = Repository<Presentation>(*m_db).get(id); p.ok()) {
            m_timeline = p.value().metadata.value("timeline").toObject();
            const QString asset = m_timeline.value("audioAssetId").toString();
            if (!asset.isEmpty()) {
                if (auto a = Repository<MediaAsset>(*m_db).get(asset); a.ok())
                    m_trackPath = a.value().filePath;
            }
        }
    }
    refresh();
}

void CueController::refresh()
{
    const ItemView *item = m_op.selectedItem();
    QList<QVariantMap> rows;
    for (const QJsonValue &v : m_timeline.value("cues").toArray()) {
        const QJsonObject cue = v.toObject();
        const int slide = cue.value("slide").toInt();
        QString label, text;
        if (item && slide >= 0 && slide < int(item->slides.size())) {
            label = tr("%1 · %2").arg(item->slides[size_t(slide)].group).arg(slide + 1);
            text = item->slides[size_t(slide)].text.section(u'\n', 0, 0);
        } else {
            label = tr("Slide %1 (gone)").arg(slide + 1);
        }
        rows << QVariantMap{{"time", timeText(cue.value("ms").toInteger())}, {"ms", cue.value("ms").toInteger()},
                            {"slide", slide}, {"label", label}, {"text", text}};
    }
    m_model.updateRows(rows);
    emit changed();
}

bool CueController::save()
{
    if (!m_db || m_presentationId.isEmpty())
        return false;
    // Cues in time order.
    QJsonArray cues = m_timeline.value("cues").toArray();
    QList<QJsonObject> sorted;
    for (const QJsonValue &v : cues)
        sorted << v.toObject();
    std::stable_sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value("ms").toInteger() < b.value("ms").toInteger();
    });
    cues = {};
    for (const QJsonObject &c : sorted)
        cues.append(c);
    m_timeline.insert("cues", cues);
    auto p = Repository<Presentation>(*m_db).get(m_presentationId);
    if (!p.ok())
        return false;
    p.value().metadata.insert("timeline", m_timeline);
    if (!Repository<Presentation>(*m_db).update(p.value()).ok()) {
        m_message = tr("The cues could not be saved");
        return false;
    }
    refresh();
    return true;
}

bool CueController::setTrack(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!m_db || m_presentationId.isEmpty() || !QFileInfo(path).isFile()) {
        m_message = tr("Choose an audio file");
        emit changed();
        return false;
    }
    const QString absolute = QFileInfo(path).absoluteFilePath();
    QString assetId;
    if (auto assets = Repository<MediaAsset>(*m_db).list(); assets.ok()) {
        for (const MediaAsset &a : assets.value())
            assetId = a.filePath == absolute ? a.id : assetId;
    }
    if (assetId.isEmpty()) {
        auto p = Repository<Presentation>(*m_db).get(m_presentationId);
        auto created = Repository<MediaAsset>(*m_db).create(
            {.libraryId = p.ok() ? p.value().libraryId : QString(), .filePath = absolute, .hash = MediaLibrary::fingerprint(absolute)});
        if (!created.ok())
            return false;
        assetId = created.value().id;
    }
    m_timeline.insert("audioAssetId", assetId);
    m_trackPath = absolute;
    m_message.clear();
    return save();
}

void CueController::clearTrack()
{
    m_timeline.remove("audioAssetId");
    m_trackPath.clear();
    save();
}

qint64 CueController::recordingPosition() const
{
    if (m_clock)
        return m_clock();
    if (m_player)
        return m_player->position();
    return m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
}

bool CueController::startRecording()
{
    if (m_presentationId.isEmpty() || m_recording)
        return false;
    // From the start: the old cues go.
    m_timeline.insert("cues", QJsonArray{});
    m_recording = true;
    m_elapsed.start();
    if (!m_trackPath.isEmpty() && !m_clock) {
        m_player = std::make_unique<QMediaPlayer>();
        m_output = std::make_unique<QAudioOutput>();
        m_player->setAudioOutput(m_output.get());
        m_player->setSource(QUrl::fromLocalFile(m_trackPath));
        m_player->play();
    }
    m_tick.start();
    m_message = tr("Recording: send each slide live when its moment comes");
    refresh();
    return true;
}

void CueController::stopRecording()
{
    if (!m_recording)
        return;
    m_recording = false;
    m_tick.stop();
    if (m_player)
        m_player->stop();
    m_player.reset();
    m_output.reset();
    m_message = tr("%n cues recorded", nullptr, int(m_timeline.value("cues").toArray().size()));
    save();
}

void CueController::onSlideSent(int index)
{
    if (!m_recording)
        return;
    QJsonArray cues = m_timeline.value("cues").toArray();
    cues.append(QJsonObject{{"ms", recordingPosition()}, {"slide", index}});
    m_timeline.insert("cues", cues);
    refresh();
}

void CueController::nudge(int row, int frames)
{
    QJsonArray cues = m_timeline.value("cues").toArray();
    if (row < 0 || row >= cues.size())
        return;
    QJsonObject cue = cues[row].toObject();
    cue.insert("ms", std::max<qint64>(0, cue.value("ms").toInteger() + frames * frameMs));
    cues[row] = cue;
    m_timeline.insert("cues", cues);
    save();
}

void CueController::removeCue(int row)
{
    QJsonArray cues = m_timeline.value("cues").toArray();
    if (row < 0 || row >= cues.size())
        return;
    cues.removeAt(row);
    m_timeline.insert("cues", cues);
    save();
}

QJsonObject CueController::liveTimeline() const
{
    return {{"audio", m_trackPath}, {"cues", m_timeline.value("cues").toArray()}};
}

bool CueController::play()
{
    if (m_recording)
        stopRecording();
    if (!m_op.playCues(liveTimeline())) {
        m_message = tr("Record some cues first");
        emit changed();
        return false;
    }
    m_message.clear();
    emit changed();
    return true;
}

void CueController::stop()
{
    m_op.stopCues();
    emit changed();
}

} // namespace lumen::app
