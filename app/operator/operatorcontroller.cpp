#include "operatorcontroller.h"
#include "planprint.h"

#include <QRegularExpression>

#include "lumen/core/songs/chords.h"

#include "lumenfile.h"
#include "planstore.h"

#include "themes.h"

#include "lumen/core/data/repository.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>

#include <memory>

namespace lumen::app {

using namespace core::show;

OperatorController::OperatorController(ShowController &show, bool editAvailable, QObject *parent)
    : QObject(parent), m_show(show), m_editAvailable(editAvailable)
{
    m_advanceTimer.setSingleShot(true);
    connect(&m_advanceTimer, &QTimer::timeout, this, &OperatorController::advanceLive);
    connect(&m_show, &ShowController::sceneChanged, this, [this] {
        // Something else (e.g. Clear Slide) took the slide off air.
        const auto &scene = m_show.scene();
        if (scene.layer(Layer::Slide).content.isEmpty() && scene.layer(Layer::Media).content.isEmpty())
            m_onAir = false;
        if (scene.layer(Layer::Background).content.isEmpty())
            m_manualBackground = false;
        emit stateChanged();
    });
    connect(this, &OperatorController::stateChanged, this, &OperatorController::updatePreviewVideo);
    // The transition chosen last time.
    const QSettings settings;
    const QString kind = settings.value(QStringLiteral("show/transition"), QStringLiteral("cut")).toString();
    if (core::show::transitionKindFromName(kind))
        m_transition = kind;
    m_transitionMs = std::clamp(settings.value(QStringLiteral("show/transitionMs"), 600).toInt(), 100, 3000);
    applyTransition();
    m_verseTimeout.setSingleShot(true);
    m_verseTimeout.setInterval(1500);
    connect(&m_verseTimeout, &QTimer::timeout, this, [this] { m_versePending = false; });

    // Autosave: the open plan and item every 30 seconds (plan contents are
    // saved as they change).
    m_sessionTimer.setInterval(30000);
    connect(&m_sessionTimer, &QTimer::timeout, this, &OperatorController::saveSession);
    m_sessionTimer.start();

    // Timers shown in Preview and Live tick here too (the output counts on
    // its own).
    // ...and drop the timer when the slide changes to one without.
    connect(this, &OperatorController::stateChanged, this, &OperatorController::timersTicked);
    m_timerTick.setInterval(250);
    connect(&m_timerTick, &QTimer::timeout, this, [this] {
        if (!liveTimerText().isEmpty() || !previewTimerText().isEmpty())
            emit timersTicked();
    });
    m_timerTick.start();
}

QList<QVariantMap> OperatorController::planRows() const
{
    QList<QVariantMap> rows;
    if (m_plan) {
        for (const ItemView &item : m_plan->items)
            rows << QVariantMap{{"kind", item.kind}, {"title", item.title}, {"subtitle", item.subtitle},
                                {"notes", item.notes}, {"header", item.presentationId.isEmpty()}};
    }
    return rows;
}

void OperatorController::setPlan(std::optional<PlanView> plan)
{
    m_plan = std::move(plan);
    m_planModel.setRows(planRows());
    m_positionItem = m_positionSlide = -1;
    m_onAir = false;
    m_current = -1;
    selectItem(0);
    emit stateChanged();
}

void OperatorController::setSongs(const std::vector<SongRow> &songs)
{
    QList<QVariantMap> rows;
    for (const SongRow &song : songs) {
        rows << QVariantMap{{"title", song.title}, {"author", song.author},
                            {"copyright", song.copyright}, {"key", song.key},
                            {"presentationId", song.presentationId}};
    }
    m_songModel.setRows(rows);
    m_selectedSong = -1;
    emit stateChanged();
}

void OperatorController::attachLibrary(core::data::Database &db, core::songs::SongLibrary &songs)
{
    m_db = &db;
    m_songs = &songs;
    m_usage = std::make_unique<UsageLog>(db);
    searchSongs(m_search);
    refreshSavedPlans();
}

void OperatorController::refreshTexts()
{
    m_planMessage.clear(); // it was in the old language
    refreshSavedPlans();
    emit stateChanged();
}

bool OperatorController::printPlan(const QUrl &file, const QString &what)
{
    if (!m_plan)
        return false;
    const PrintKind kind = what == QLatin1String("lyrics")   ? PrintKind::Lyrics
                           : what == QLatin1String("chords") ? PrintKind::ChordCharts
                                                             : PrintKind::ServiceOrder;
    const auto written = writePlanPdf(*m_plan, kind, file.toLocalFile());
    m_planMessage = written.ok() ? tr("Saved %1; it opens to print").arg(QFileInfo(file.toLocalFile()).fileName())
                                 : tr("Not saved: %1").arg(written.error().message);
    emit stateChanged();
    if (written.ok())
        emit openFileRequested(file);
    return written.ok();
}

bool OperatorController::exportSongUsage(const QUrl &file, int months)
{
    if (!m_usage)
        return false;
    const QDate to = QDate::currentDate();
    const QDate from = months > 0 ? to.addMonths(-months).addDays(1) : QDate(2000, 1, 1);
    const auto rows = m_usage->report(from, to);
    const auto saved = rows.ok() ? m_usage->exportCsv(file.toLocalFile(), from, to) : core::Status(rows.error());
    if (!saved.ok()) {
        m_planMessage = tr("Song usage not saved: %1").arg(saved.error().message);
    } else {
        int uses = 0;
        for (const auto &row : rows.value())
            uses += row.uses;
        m_planMessage = rows.value().empty() ? tr("Song usage saved: no songs were shown in that time")
                                             : tr("Song usage saved: %1, %2")
                                                   .arg(rows.value().size() == 1 ? tr("1 song") : tr("%1 songs").arg(rows.value().size()),
                                                        uses == 1 ? tr("1 use") : tr("%1 uses").arg(uses));
    }
    emit stateChanged();
    return saved.ok();
}

void OperatorController::searchSongs(const QString &query)
{
    m_search = query;
    if (!m_songs)
        return;
    auto hits = m_songs->search(query);
    if (!hits.ok()) {
        qWarning("Song search failed: %s", qPrintable(hits.error().message));
        return;
    }
    std::vector<SongRow> rows;
    for (const auto &hit : hits.value())
        rows.push_back({hit.title, hit.author, hit.copyright, hit.key, hit.presentationId});
    setSongs(rows);
}

bool OperatorController::showSong(int row)
{
    const QString id = m_songModel.get(row).value(QStringLiteral("presentationId")).toString();
    if (!m_db || id.isEmpty())
        return false;
    auto view = loadPresentationView(*m_db, id);
    if (!view.ok()) {
        qWarning("Song not shown: %s", qPrintable(view.error().message));
        m_planMessage = tr("The song could not be opened: %1").arg(view.error().message);
        emit stateChanged();
        return false;
    }
    m_librarySong = std::move(view).value();
    m_selectedSong = row;
    m_current = libraryItem;
    showSelected();
    return true;
}

void OperatorController::attachMedia(MediaLibrary &media)
{
    m_mediaLibrary = &media;
    refreshMedia();
}

void OperatorController::refreshMedia()
{
    if (!m_mediaLibrary)
        return;
    auto rows = m_mediaLibrary->list();
    const auto loopChosen = [this](const QString &presentationId) {
        if (!m_db)
            return false;
        const auto p = core::data::Repository<core::data::Presentation>(*m_db).get(presentationId);
        return p.ok() && p.value().metadata.value(QStringLiteral("videoLoop")).toBool();
    };
    QList<QVariantMap> list;
    if (rows.ok()) {
        for (const MediaRow &row : rows.value()) {
            const qint64 seconds = (row.durationMs + 500) / 1000;
            list << QVariantMap{{"title", row.title},
                                {"kind", row.kind},
                                {"duration", row.kind == QLatin1String("video")
                                                 ? QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'))
                                                 : QString()},
                                {"thumbnail", row.thumbnail.isEmpty() ? QUrl() : QUrl::fromLocalFile(row.thumbnail)},
                                {"presentationId", row.presentationId},
                                {"path", row.path},
                                // Shown as "Loop" on the card: the operator's choice.
                                {"loop", loopChosen(row.presentationId)}};
        }
    }
    m_mediaModel.setRows(list);
    m_selectedMedia = -1;
    emit stateChanged();
}

QString OperatorController::importMedia(const QList<QUrl> &urls)
{
    if (!m_mediaLibrary)
        return {};
    QStringList paths;
    for (const QUrl &url : urls)
        paths << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    const MediaImportReport report = m_mediaLibrary->importFiles(paths);
    m_mediaSummary = report.summary();
    refreshMedia();
    return m_mediaSummary;
}

bool OperatorController::showMedia(int row)
{
    const QString id = m_mediaModel.get(row).value(QStringLiteral("presentationId")).toString();
    if (!m_db || id.isEmpty())
        return false;
    auto view = loadPresentationView(*m_db, id);
    if (!view.ok() || !showItem(std::move(view).value()))
        return false;
    m_selectedMedia = row;
    emit stateChanged();
    return true;
}

bool OperatorController::playMedia(int row)
{
    if (!showMedia(row))
        return false;
    return m_editMode || goLive(0);
}

void OperatorController::preloadMedia(int row)
{
    const QVariantMap media = m_mediaModel.get(row);
    if (media.value(QStringLiteral("kind")) == QLatin1String("video"))
        emit previewVideoChanged(media.value(QStringLiteral("path")).toString());
}

bool OperatorController::shownIsVideo() const
{
    const ItemView *view = selected();
    return view && !view->slides.empty() && view->slides.front().mediaKind == QLatin1String("video");
}

bool OperatorController::liveVideo() const
{
    const Content &media = m_show.scene().layer(Layer::Media).content;
    return media.kind == Content::Kind::Video && media.videoPath == m_livePath;
}

void OperatorController::toggleVideoPlaying()
{
    if (!liveVideo())
        return;
    m_livePlaying = !m_livePlaying; // at once; the next status confirms it
    emit videoControlRequested(m_livePlaying ? QStringLiteral("play") : QStringLiteral("pause"), 0);
    emit videoPositionChanged();
}

void OperatorController::seekVideo(double positionMs)
{
    if (!liveVideo())
        return;
    m_livePositionMs = qBound<qint64>(0, qint64(positionMs), m_liveDurationMs);
    emit videoControlRequested(QStringLiteral("seek"), m_livePositionMs);
    emit videoPositionChanged();
}

QString OperatorController::formatTime(qint64 ms)
{
    const qint64 seconds = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

qint64 OperatorController::parseTime(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return 0;
    static const QRegularExpression form(QStringLiteral(R"(^(?:(\d+):)?(\d+(?:\.\d+)?)$)"));
    const auto m = form.match(t);
    if (!m.hasMatch())
        return -1;
    const double seconds = m.captured(1).toDouble() * 60 + m.captured(2).toDouble();
    if (!m.captured(1).isEmpty() && m.captured(2).toDouble() >= 60)
        return -1;
    return qint64(seconds * 1000 + 0.5);
}

bool OperatorController::shownIsSong() const
{
    const ItemView *view = selected();
    return view && view->kind == QLatin1String("SONG");
}

int OperatorController::shownTranspose() const
{
    return shownIsSong() ? selected()->transpose : 0;
}

QString OperatorController::shownKey() const
{
    if (!shownIsSong() || selected()->key.isEmpty())
        return {};
    return core::songs::transposeKey(selected()->key, selected()->transpose);
}

void OperatorController::transposeShown(int semitones)
{
    if (!shownIsSong() || !m_db || semitones == 0)
        return;
    const int transpose = std::clamp(selected()->transpose + semitones, -11, 11);
    const QString presentationId = selected()->presentationId;
    using core::data::Presentation;
    using core::data::Repository;
    if (auto p = Repository<Presentation>(*m_db).get(presentationId); p.ok()) {
        p.value().metadata.insert(QStringLiteral("transpose"), transpose);
        if (auto updated = Repository<Presentation>(*m_db).update(p.value()); !updated.ok())
            qWarning("Transposition not saved: %s", qPrintable(updated.error().message));
    }
    const auto apply = [&](ItemView &view) {
        if (view.presentationId == presentationId)
            view.transpose = transpose;
    };
    if (m_plan) {
        for (ItemView &item : m_plan->items)
            apply(item);
    }
    if (m_librarySong)
        apply(*m_librarySong);
    emit stateChanged();
}

QString OperatorController::chordsForStage(const QString &chordPro, int transpose, const QString &key)
{
    if (chordPro.isEmpty())
        return {};
    const bool flats = key.isEmpty() ? chordPro.contains(QLatin1String("b]")) || chordPro.contains(QLatin1String("b/"))
                                     : core::songs::keyUsesFlats(core::songs::transposeKey(key, transpose));
    const QString moved = core::songs::transposeChordPro(chordPro, transpose, flats);
    static const QRegularExpression chord(QStringLiteral(R"(\[([^\[\]\s]{1,12})\])"));
    QStringList lines;
    for (const QString &line : moved.split(u'\n')) {
        QString html;
        qsizetype from = 0;
        auto it = chord.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            if (!core::songs::isChord(m.captured(1)))
                continue;
            html += line.mid(from, m.capturedStart() - from).toHtmlEscaped();
            html += QStringLiteral("<font color=\"#F2C38B\">%1</font>\u2009").arg(m.captured(1).toHtmlEscaped());
            from = m.capturedEnd();
        }
        lines << html + line.mid(from).toHtmlEscaped();
    }
    return lines.join(QStringLiteral("<br>"));
}

QString OperatorController::shownVideoStart() const
{
    return shownIsVideo() ? formatTime(selected()->slides.front().trimInMs) : QString();
}

QString OperatorController::shownVideoEnd() const
{
    if (!shownIsVideo() || selected()->slides.front().trimOutMs <= 0)
        return {};
    return formatTime(selected()->slides.front().trimOutMs);
}

bool OperatorController::setShownVideoTrim(const QString &start, const QString &end)
{
    if (!shownIsVideo() || !m_db)
        return false;
    const qint64 in = parseTime(start);
    const qint64 out = parseTime(end);
    if (in < 0 || out < 0 || (out > 0 && out <= in)) {
        m_planMessage = tr("Start and end are times like 1:05, and the end comes after the start");
        emit stateChanged();
        return false;
    }
    const QString presentationId = selected()->presentationId;
    const QString path = selected()->slides.front().mediaPath;
    using core::data::Presentation;
    using core::data::Repository;
    if (auto p = Repository<Presentation>(*m_db).get(presentationId); p.ok()) {
        p.value().metadata.insert(QStringLiteral("trimInMs"), in);
        p.value().metadata.insert(QStringLiteral("trimOutMs"), out);
        if (auto updated = Repository<Presentation>(*m_db).update(p.value()); !updated.ok())
            qWarning("Start and end not saved: %s", qPrintable(updated.error().message));
    }
    const auto apply = [&](ItemView &view) {
        if (view.presentationId != presentationId)
            return;
        for (SlideView &slide : view.slides) {
            slide.trimInMs = in;
            slide.trimOutMs = out;
        }
    };
    if (m_plan) {
        for (ItemView &item : m_plan->items)
            apply(item);
    }
    if (m_librarySong)
        apply(*m_librarySong);
    // Playing now: it takes the points at once.
    const Content &onAir = m_show.scene().layer(Layer::Media).content;
    if (onAir.kind == Content::Kind::Video && onAir.videoPath == path) {
        Content changed = onAir;
        changed.trimInMs = in;
        changed.trimOutMs = out;
        m_show.setContent(Layer::Media, changed);
    }
    m_planMessage.clear();
    emit stateChanged();
    return true;
}

bool OperatorController::shownVideoLoops() const
{
    return shownIsVideo() && selected()->slides.front().mediaLoop;
}

void OperatorController::setShownVideoLoops(bool loops)
{
    if (!shownIsVideo() || !m_db || shownVideoLoops() == loops)
        return;
    const QString presentationId = selected()->presentationId;
    const QString path = selected()->slides.front().mediaPath;
    // Kept with the video's presentation: in the library and every plan.
    using core::data::Presentation;
    using core::data::Repository;
    if (auto p = Repository<Presentation>(*m_db).get(presentationId); p.ok()) {
        p.value().metadata.insert(QStringLiteral("videoLoop"), loops);
        if (auto updated = Repository<Presentation>(*m_db).update(p.value()); !updated.ok())
            qWarning("Loop setting not saved: %s", qPrintable(updated.error().message));
    }
    const auto apply = [&](ItemView &view) {
        if (view.presentationId != presentationId)
            return;
        for (SlideView &slide : view.slides)
            slide.mediaLoop = loops;
    };
    if (m_plan) {
        for (ItemView &item : m_plan->items)
            apply(item);
    }
    if (m_librarySong)
        apply(*m_librarySong);
    // The card's badge.
    QList<QVariantMap> rows;
    for (int i = 0; i < m_mediaModel.count(); ++i) {
        QVariantMap row = m_mediaModel.get(i);
        if (row.value(QStringLiteral("presentationId")).toString() == presentationId)
            row.insert(QStringLiteral("loop"), loops);
        rows << row;
    }
    m_mediaModel.updateRows(rows);
    // Playing now: it takes the setting at once (from where it is).
    const Content &onAir = m_show.scene().layer(Layer::Media).content;
    if (onAir.kind == Content::Kind::Video && onAir.videoPath == path) {
        Content changed = onAir;
        changed.loop = loops;
        m_show.setContent(Layer::Media, changed);
    }
    emit stateChanged();
}

QVariantList OperatorController::transitions() const
{
    return {QVariantMap{{"key", "cut"}, {"name", tr("Cut")}},
            QVariantMap{{"key", "fade"}, {"name", tr("Fade")}},
            QVariantMap{{"key", "push"}, {"name", tr("Push")}},
            QVariantMap{{"key", "pushUp"}, {"name", tr("Push up")}},
            QVariantMap{{"key", "wipe"}, {"name", tr("Wipe")}},
            QVariantMap{{"key", "zoom"}, {"name", tr("Zoom")}}};
}

void OperatorController::setTransition(const QString &kind)
{
    if (kind == m_transition || !core::show::transitionKindFromName(kind))
        return;
    m_transition = kind;
    QSettings().setValue(QStringLiteral("show/transition"), kind);
    applyTransition();
    emit transitionChanged();
}

void OperatorController::setTransitionMs(int ms)
{
    ms = std::clamp(ms, 100, 3000);
    if (ms == m_transitionMs)
        return;
    m_transitionMs = ms;
    QSettings().setValue(QStringLiteral("show/transitionMs"), ms);
    applyTransition();
    emit transitionChanged();
}

void OperatorController::applyTransition()
{
    // Only a real change is a change of scene (a cut at start sends nothing).
    const auto kind = *core::show::transitionKindFromName(m_transition);
    const auto transition = kind == core::show::TransitionKind::Cut ? core::show::Transition::cut()
                                                                    : core::show::Transition::of(kind, m_transitionMs);
    for (const Layer layer : {Layer::Slide, Layer::Media}) {
        if (m_show.scene().layer(layer).transition != transition)
            m_show.setTransition(layer, transition);
    }
}

void OperatorController::stepItem(int direction)
{
    if (!m_plan || m_plan->items.empty())
        return;
    // Where to step from: the plan item shown, else (a library song or
    // scripture is shown) the one that was live, else the start.
    int from = m_current >= 0 ? m_current : m_positionItem >= 0 ? m_positionItem : (direction > 0 ? -1 : int(m_plan->items.size()));
    for (int index = from + direction; index >= 0 && index < int(m_plan->items.size()); index += direction) {
        if (!m_plan->items[std::size_t(index)].slides.empty()) {
            selectItem(index);
            return;
        }
    }
}

bool OperatorController::playItem(int index)
{
    selectItem(index);
    const ItemView *view = selected();
    if (m_editMode || m_current != index || !view || view->slides.empty())
        return false;
    return goLive(0);
}

bool OperatorController::playSong(int row)
{
    if (!showSong(row))
        return false;
    return m_editMode || goLive(0);
}

bool OperatorController::useAsBackground(int row)
{
    // Edit mode never changes the screen.
    if (m_editMode)
        return false;
    const QVariantMap media = m_mediaModel.get(row);
    if (media.isEmpty())
        return false;
    const QString path = media.value(QStringLiteral("path")).toString();
    const Content background = media.value(QStringLiteral("kind")) == QLatin1String("video")
                                   ? Content::video(path, true, 0.0, media.value("thumbnail").toUrl().toLocalFile())
                                   : Content::image(path);
    m_manualBackground = true;
    m_show.setContent(Layer::Background, background);
    m_selectedMedia = row;
    emit stateChanged();
    return true;
}

void OperatorController::setLogoMedia(const QString &path)
{
    using core::show::Content;
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        m_logoMedia.clear();
        m_show.setLogoContent(core::show::Scene{}.logoContent);
    } else {
        m_logoMedia = path;
        m_show.setLogoContent(MediaLibrary::isVideo(path) ? Content::video(path, true, 0.0) : Content::image(path));
    }
    emit stateChanged();
}

bool OperatorController::useAsLogo(int row)
{
    const QString path = m_mediaModel.get(row).value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return false;
    setLogoMedia(path);
    emit logoMediaChanged(m_logoMedia);
    return true;
}

void OperatorController::resetLogo()
{
    setLogoMedia({});
    emit logoMediaChanged({});
}

bool OperatorController::removeMedia(int row)
{
    const QVariantMap media = m_mediaModel.get(row);
    const QString id = media.value(QStringLiteral("presentationId")).toString();
    if (!m_mediaLibrary || id.isEmpty())
        return false;
    // Off the screen first if it is up there.
    const QString path = media.value(QStringLiteral("path")).toString();
    for (const Layer layer : {Layer::Media, Layer::Background}) {
        const Content &content = m_show.scene().layer(layer).content;
        if (!path.isEmpty() && (content.videoPath == path || content.imagePath == path))
            m_show.setContent(layer, Content::empty());
    }
    const auto removed = m_mediaLibrary->remove(id);
    if (!removed.ok()) {
        m_mediaSummary = tr("Not removed: %1").arg(removed.error().message);
        emit stateChanged();
        return false;
    }
    m_mediaSummary = removed.value() > 0
                         ? tr("Removed \"%1\" from the library and %2 plan item(s)").arg(media.value(QStringLiteral("title")).toString()).arg(removed.value())
                         : tr("Removed \"%1\" from the library (the file is still on disk)").arg(media.value(QStringLiteral("title")).toString());
    if (m_selectedMedia == row)
        m_selectedMedia = -1;
    if (m_librarySong && m_librarySong->presentationId == id) {
        m_librarySong.reset();
        if (m_current == libraryItem)
            m_current = -1;
    }
    refreshMedia();
    if (m_plan && removed.value() > 0)
        reloadPlan(m_current < int(m_plan->items.size()) ? m_current : -1);
    emit stateChanged();
    return true;
}

bool OperatorController::addMediaToPlan(int row)
{
    return addPresentationToPlan(m_mediaModel.get(row).value(QStringLiteral("presentationId")).toString());
}

void OperatorController::attachPresentations(PresentationLibrary &decks, DeckImporter::Options options)
{
    m_decks = &decks;
    m_deckOptions = options;
    refreshDecks();
}

void OperatorController::refreshDecks()
{
    if (!m_decks)
        return;
    auto rows = m_decks->list();
    QList<QVariantMap> list;
    if (rows.ok()) {
        for (const DeckRow &row : rows.value()) {
            list << QVariantMap{{"title", row.title},
                                {"slides", row.slides},
                                {"thumbnail", row.thumbnail.isEmpty() ? QUrl() : QUrl::fromLocalFile(row.thumbnail)},
                                {"presentationId", row.presentationId},
                                {"method", row.method}};
        }
    }
    m_deckModel.setRows(list);
    m_selectedDeck = -1;
    emit stateChanged();
}

bool OperatorController::importPresentations(const QList<QUrl> &urls)
{
    if (!m_decks || m_decksBusy)
        return false;
    QStringList paths;
    for (const QUrl &url : urls)
        paths << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    m_decksBusy = true;
    m_deckSummary = tr("Importing…");
    emit stateChanged();

    // Drawing runs on a worker thread; storing happens back here, on the
    // database's thread.
    auto rendered = std::make_shared<std::vector<PresentationLibrary::Rendered>>();
    QThread *worker = QThread::create([rendered, paths, known = m_decks->knownHashes(),
                                       folder = m_decks->slideFolder(), options = m_deckOptions] {
        *rendered = PresentationLibrary::render(paths, known, folder, options);
    });
    connect(worker, &QThread::finished, this, [this, worker, rendered] {
        worker->deleteLater();
        const DeckImportReport report = m_decks->store(*rendered);
        m_decksBusy = false;
        m_deckSummary = report.summary();
        m_deckDetails = report.messages.join(u'\n');
        refreshDecks();
        emit decksImported(m_deckSummary);
    });
    worker->start();
    return true;
}

bool OperatorController::showDeck(int row)
{
    const QString id = m_deckModel.get(row).value(QStringLiteral("presentationId")).toString();
    if (!m_db || id.isEmpty())
        return false;
    auto view = loadPresentationView(*m_db, id);
    if (!view.ok() || !showItem(std::move(view).value()))
        return false;
    m_selectedDeck = row;
    emit stateChanged();
    return true;
}

bool OperatorController::addDeckToPlan(int row)
{
    return addPresentationToPlan(m_deckModel.get(row).value(QStringLiteral("presentationId")).toString());
}

QString OperatorController::previewVideo() const
{
    const ItemView *view = selected();
    const int index = previewSlide();
    if (!view || index < 0)
        return {};
    const SlideView &slide = view->slides[std::size_t(index)];
    return slide.mediaKind == QLatin1String("video") ? slide.mediaPath : QString();
}

void OperatorController::updatePreviewVideo()
{
    const QString path = previewVideo();
    if (path == m_previewVideo)
        return;
    m_previewVideo = path;
    if (!path.isEmpty())
        emit previewVideoChanged(path);
}

QUrl OperatorController::previewPoster() const
{
    const ItemView *view = selected();
    const int index = previewSlide();
    if (!view || index < 0 || view->slides[std::size_t(index)].thumbnail.isEmpty())
        return {};
    return QUrl::fromLocalFile(view->slides[std::size_t(index)].thumbnail);
}

void OperatorController::setOutputStatus(const QJsonObject &status)
{
    const QJsonObject video = status.value(QStringLiteral("videoPosition")).toObject();
    const QString path = video.value(QStringLiteral("path")).toString();
    const qint64 position = video.value(QStringLiteral("positionMs")).toInteger();
    const qint64 duration = video.value(QStringLiteral("durationMs")).toInteger();
    const bool playing = video.value(QStringLiteral("playing")).toBool();
    if (path != m_livePath || position != m_livePositionMs || duration != m_liveDurationMs || playing != m_livePlaying) {
        const bool otherVideo = path != m_livePath; // liveVideo may change
        m_livePath = path;
        m_livePositionMs = position;
        m_liveDurationMs = duration;
        m_livePlaying = playing;
        emit videoPositionChanged();
        if (otherVideo)
            emit stateChanged();
    }

    QHash<QString, QString> errors;
    for (const QJsonValue &v : status.value(QStringLiteral("videoErrors")).toArray())
        errors.insert(v.toObject().value(QStringLiteral("path")).toString(), v.toObject().value(QStringLiteral("message")).toString());
    if (errors == m_videoErrors)
        return;
    m_videoErrors = errors;
    emit stateChanged();
}

QString OperatorController::problemFor(const QString &path) const
{
    const auto it = m_videoErrors.constFind(path);
    if (path.isEmpty() || it == m_videoErrors.cend())
        return {};
    return tr("\"%1\" cannot be played: %2").arg(QFileInfo(path).fileName(), it.value());
}

QString OperatorController::liveProblem() const
{
    for (const Layer layer : {Layer::Media, Layer::Background}) {
        const Content &content = m_show.scene().layer(layer).content;
        if (content.kind == Content::Kind::Video) {
            if (const QString problem = problemFor(content.videoPath); !problem.isEmpty())
                return problem;
        }
    }
    return {};
}

QString OperatorController::previewProblem() const
{
    const ItemView *view = selected();
    const int index = previewSlide();
    if (!view || index < 0 || index >= int(view->slides.size()))
        return {};
    const SlideView &slide = view->slides[std::size_t(index)];
    return slide.mediaKind == QLatin1String("video") ? problemFor(slide.mediaPath) : QString();
}

QUrl OperatorController::livePoster() const
{
    const Content &media = m_show.scene().layer(Layer::Media).content;
    if (media.kind == Content::Kind::Image)
        return QUrl::fromLocalFile(media.imagePath);
    if (media.kind == Content::Kind::Video && !media.imagePath.isEmpty())
        return QUrl::fromLocalFile(media.imagePath);
    return {};
}

bool OperatorController::showItem(ItemView item)
{
    if (item.slides.empty())
        return false;
    m_librarySong = std::move(item);
    m_selectedSong = -1;
    m_current = libraryItem;
    showSelected();
    return true;
}

void OperatorController::refreshThemes()
{
    if (!m_db)
        return;
    auto restyle = [this](ItemView &item) {
        if (item.presentationId.isEmpty() || item.presentationId.startsWith(QLatin1String("scripture:")))
            return;
        auto presentation = core::data::Repository<core::data::Presentation>(*m_db).get(item.presentationId);
        if (!presentation.ok())
            return;
        if (const auto theme = themeFor(*m_db, presentation.value()))
            applyTheme(item, theme->style, theme->background);
    };
    if (m_plan) {
        for (ItemView &item : m_plan->items)
            restyle(item);
    }
    if (m_librarySong)
        restyle(*m_librarySong);
    emit stateChanged();
}

bool OperatorController::addSongToPlan(int row)
{
    return addPresentationToPlan(m_songModel.get(row).value(QStringLiteral("presentationId")).toString());
}

bool OperatorController::addPresentationToPlan(const QString &id)
{
    using namespace core::data;
    if (!m_db || id.isEmpty())
        return false;

    PlanItem added;
    const auto stored = m_db->inTransaction([&]() -> core::Status {
        if (!m_plan) {
            auto plan = Repository<Plan>(*m_db).create({.name = "Sunday Service", .date = QDate::currentDate()});
            if (!plan.ok())
                return plan.error();
            m_plan = PlanView{plan.value().id, plan.value().name, plan.value().date, {}};
        }
        auto item = Repository<PlanItem>(*m_db).create(
            {.planId = m_plan->id, .position = int(m_plan->items.size()), .presentationId = id});
        if (!item.ok())
            return item.error();
        added = item.value();
        return core::success();
    });
    if (!stored.ok()) {
        qWarning("Song not added to the plan: %s", qPrintable(stored.error().message));
        m_planMessage = tr("Not added to the plan: %1").arg(stored.error().message);
        emit stateChanged();
        return false;
    }
    auto view = loadPresentationView(*m_db, id);
    if (!view.ok())
        return false;
    view.value().planItemId = added.id;
    m_plan->items.push_back(std::move(view).value());

    m_planModel.setRows(planRows());
    refreshSavedPlans();
    if (m_current == -1)
        selectItem(0);
    emit stateChanged();
    return true;
}

QString OperatorController::importSongs(const QList<QUrl> &urls)
{
    if (!m_songs)
        return {};
    QStringList paths;
    for (const QUrl &url : urls)
        paths << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    const auto report = m_songs->importFiles(paths);
    m_importSummary = report.summary();
    m_importDetails = report.messages.join(u'\n');
    searchSongs(m_search);
    emit stateChanged();
    return m_importSummary;
}

void OperatorController::setOutputConnected(bool connected)
{
    if (connected == m_outputConnected)
        return;
    m_outputConnected = connected;
    emit stateChanged();
}

const ItemView *OperatorController::item(int index) const
{
    if (index == libraryItem)
        return m_librarySong ? &*m_librarySong : nullptr;
    if (!m_plan || index < 0 || index >= int(m_plan->items.size()))
        return nullptr;
    return &m_plan->items[std::size_t(index)];
}

QString OperatorController::planName() const
{
    return m_plan ? m_plan->name : tr("No service plan");
}

QString OperatorController::planDetail() const
{
    if (!m_plan)
        return tr("Create or open a plan to begin");
    const QString date = m_plan->date.isValid()
                             ? QLocale().toString(m_plan->date, QLocale::ShortFormat)
                             : tr("No date");
    const int items = int(m_plan->items.size());
    if (items == 1)
        return tr("%1 · 1 item").arg(date);
    return tr("%1 · %2 items").arg(date).arg(items);
}

QString OperatorController::itemTitle() const
{
    const ItemView *view = selected();
    return view ? view->title : QString();
}

QString OperatorController::itemDetail() const
{
    const ItemView *view = selected();
    return view ? view->detail : QString();
}

QVariantList OperatorController::arrangement() const
{
    QVariantList chips;
    if (const ItemView *view = selected()) {
        for (const ArrangementChip &chip : view->chips) {
            chips << QVariantMap{{"label", chip.label},
                                 {"background", chip.background},
                                 {"foreground", chip.foreground}};
        }
    }
    return chips;
}

QString OperatorController::keyOf(const ItemView &item)
{
    return item.planItemId.isEmpty() ? QStringLiteral("song:") + item.presentationId
                                     : QStringLiteral("plan:") + item.planItemId;
}

bool OperatorController::positionIsSelected() const
{
    const ItemView *view = selected();
    return view && m_positionItem == m_current && keyOf(*view) == m_positionKey;
}

int OperatorController::liveSlide() const
{
    return m_onAir && positionIsSelected() ? m_positionSlide : -1;
}

int OperatorController::previewSlide() const
{
    const ItemView *view = selected();
    if (!view || view->slides.empty())
        return -1;
    if (positionIsSelected() && m_positionSlide >= 0) {
        const int next = m_positionSlide + 1;
        return next < int(view->slides.size()) ? next : -1;
    }
    return 0;
}

QString OperatorController::liveText() const
{
    const Content &content = m_show.scene().layer(Layer::Slide).content;
    // A countdown slide's text is its label.
    return content.kind == Content::Kind::Text || content.kind == Content::Kind::Timer ? content.text : QString();
}

QString OperatorController::previewText() const
{
    const ItemView *view = selected();
    const int index = previewSlide();
    return view && index >= 0 ? view->slides[std::size_t(index)].text : QString();
}

QVariantMap OperatorController::previewStyle() const
{
    const ItemView *view = selected();
    const int index = previewSlide();
    if (!view || index < 0 || !view->style)
        return {};
    return styleWithSize(*view->style, view->slides[std::size_t(index)].fontSize).toVariantMap();
}

QString OperatorController::previewFooter() const
{
    const ItemView *view = selected();
    const int index = previewSlide();
    return view && index >= 0 ? view->slides[std::size_t(index)].footer : QString();
}

QColor OperatorController::previewBackground() const
{
    const ItemView *view = selected();
    if (view) {
        if (const auto background = backgroundContent(view->background); background && background->kind == Content::Kind::Colour)
            return background->colour;
    }
    return QColor(0x14, 0x21, 0x3D);
}

QVariantMap OperatorController::liveStyle() const
{
    return m_show.scene().layer(Layer::Slide).content.style.toVariantMap();
}

QString OperatorController::liveFooter() const
{
    return m_show.scene().layer(Layer::Slide).content.footer;
}

QColor OperatorController::liveBackground() const
{
    const Content &background = m_show.scene().layer(Layer::Background).content;
    return background.kind == Content::Kind::Colour ? background.colour : QColor(0x14, 0x21, 0x3D);
}

QString OperatorController::liveLabel() const
{
    return m_onAir ? m_positionLabel : tr("Clear");
}

QVariantList OperatorController::layers() const
{
    auto describe = [this](Layer layer) -> QVariantMap {
        const Content &content = m_show.scene().layer(layer).content;
        QString value;
        switch (content.kind) {
        case Content::Kind::Empty: value = tr("Off"); break;
        case Content::Kind::Colour: value = tr("Colour %1").arg(content.colour.name().toUpper()); break;
        case Content::Kind::Image: value = QFileInfo(content.imagePath).fileName(); break;
        case Content::Kind::Video:
            value = (content.loop ? tr("Motion loop: %1") : tr("Video: %1")).arg(QFileInfo(content.videoPath).completeBaseName());
            break;
        case Content::Kind::Text:
            value = (layer == Layer::Slide && m_onAir) ? liveLabel() : content.text.section(u'\n', 0, 0);
            break;
        case Content::Kind::Timer:
            value = content.text.isEmpty() ? tr("Timer") : tr("Timer: %1").arg(content.text.section(u'\n', 0, 0));
            break;
        case Content::Kind::Cued:
            value = tr("Following the backing track");
            break;
        case Content::Kind::Composed:
            value = (layer == Layer::Slide && m_onAir) ? liveLabel() : tr("Slide");
            break;
        case Content::Kind::Prop: {
            QStringList parts;
            if (content.prop.contains("lowerThird"))
                parts << tr("Lower third: %1").arg(content.prop.value("lowerThird").toObject().value("line1").toString());
            if (content.prop.contains("bug"))
                parts << tr("Logo");
            value = parts.join(QStringLiteral(" + "));
            break;
        }
        }
        return {{"name", layerName(layer).replace(0, 1, layerName(layer).at(0).toUpper())},
                {"value", value},
                {"active", !content.isEmpty()}};
    };
    // Top to bottom, as in the mockup.
    return {describe(Layer::Messages), describe(Layer::Props), describe(Layer::Slide),
            describe(Layer::Media), describe(Layer::Background)};
}

void OperatorController::setMode(const QString &mode)
{
    const bool edit = mode == QLatin1String("edit") && m_editAvailable;
    if (edit == m_editMode)
        return;
    m_editMode = edit;
    m_selected = qMax(0, liveSlide());
    emit stateChanged();
}

void OperatorController::selectItem(int index)
{
    if (!item(index)) {
        // Nothing to select (a new or emptied plan): the slide grid empties
        // too, rather than keep the last item's slides.
        if (m_current == -1)
            showSelected();
        return;
    }
    if (index == m_current)
        return;
    m_current = index;
    showSelected();
}

void OperatorController::showSelected()
{
    m_selected = 0;
    QList<QVariantMap> rows;
    if (const ItemView *view = selected()) {
        for (const SlideView &slide : view->slides) {
            rows << QVariantMap{{"group", slide.group}, {"colour", slide.colour}, {"text", slide.text},
                                {"thumbnail", slide.thumbnail.isEmpty() ? QUrl() : QUrl::fromLocalFile(slide.thumbnail)}};
        }
    }
    m_slideModel.setRows(rows);
    emit stateChanged();
}

void OperatorController::activateSlide(int index)
{
    if (m_editMode) {
        const ItemView *view = selected();
        if (view && index >= 0 && index < int(view->slides.size())) {
            m_selected = index;
            emit stateChanged();
        }
        return;
    }
    goLive(index);
}

bool OperatorController::goLive(int index)
{
    const ItemView *view = selected();
    return view && goLiveFrom(*view, m_current, index);
}

const ItemView *OperatorController::liveView() const
{
    if (!m_onAir)
        return nullptr;
    const ItemView *view = m_positionItem == libraryItem ? (m_librarySong ? &*m_librarySong : nullptr) : item(m_positionItem);
    return view && keyOf(*view) == m_positionKey ? view : nullptr;
}

void OperatorController::restartAdvance(const ItemView &view)
{
    if (view.autoAdvance > 0 && view.slides.size() > 1)
        m_advanceTimer.start(view.autoAdvance * 1000);
    else
        m_advanceTimer.stop();
}

void OperatorController::advanceLive()
{
    // Only while that item is still up (not cleared, not replaced).
    const ItemView *view = liveView();
    if (!view || view->autoAdvance <= 0 || m_editMode
        || (m_show.scene().layer(Layer::Slide).content.isEmpty() && m_show.scene().layer(Layer::Media).content.isEmpty()))
        return;
    const int next = (m_positionSlide + 1) % int(view->slides.size()); // round and round
    goLiveFrom(*view, m_positionItem, next);
}

bool OperatorController::goLiveFrom(const ItemView &item, int itemIndex, int index)
{
    // Edit mode can never change what is on screen.
    const ItemView *view = &item;
    if (m_editMode || index < 0 || index >= int(view->slides.size()))
        return false;
    // CCLI reporting: a song counts once a day.
    if (m_usage && view->kind == QLatin1String("SONG")) {
        if (const auto recorded = m_usage->record(view->presentationId); !recorded.ok())
            qWarning("Song usage not recorded: %s", qPrintable(recorded.error().message));
    }
    m_positionItem = itemIndex;
    m_positionKey = keyOf(*view);
    m_positionSlide = index;
    m_positionLabel = tr("%1 · slide %2").arg(view->slides[std::size_t(index)].group).arg(index + 1);
    m_onAir = true;
    const SlideView &slide = view->slides[std::size_t(index)];

    std::vector<std::pair<Layer, Content>> changes;
    if (!slide.mediaKind.isEmpty()) {
        // A video or picture fills the Media layer; the slide text goes.
        // (slideContent: a video with its start and end points.)
        const Content media = slideContent(*view, index);
        changes = {{Layer::Media, media}, {Layer::Slide, Content::empty()}};
    } else {
        changes = {{Layer::Slide, slideContent(*view, index)}};
        // Full-screen media gives way to the next slide.
        if (!m_show.scene().layer(Layer::Media).content.isEmpty())
            changes.push_back({Layer::Media, Content::empty()});
        // A themed item brings its background, unless the operator chose one.
        if (const auto background = backgroundContent(view->background); background && !m_manualBackground)
            changes.push_back({Layer::Background, *background});
    }

    const quint64 before = m_show.scene().sequence;
    m_show.setContents(changes);
    if (m_show.scene().sequence == before)
        emit stateChanged(); // same text, new position (e.g. a repeated chorus)
    restartAdvance(*view);
    emit slideSentLive(index);
    return true;
}

bool OperatorController::canAutoAdvance() const
{
    const ItemView *view = selected();
    return view && view->slides.size() > 1 && !shownIsVideo() && view->kind != QLatin1String("BIBLE");
}

int OperatorController::shownAutoAdvance() const
{
    const ItemView *view = selected();
    return view ? view->autoAdvance : 0;
}

void OperatorController::setShownAutoAdvance(int seconds)
{
    const ItemView *view = selected();
    seconds = std::clamp(seconds, 0, 600);
    if (!view || !m_db || view->presentationId.isEmpty() || seconds == view->autoAdvance)
        return;
    const QString presentationId = view->presentationId;
    using core::data::Presentation;
    using core::data::Repository;
    if (auto p = Repository<Presentation>(*m_db).get(presentationId); p.ok()) {
        p.value().metadata.insert(QStringLiteral("autoAdvance"), seconds);
        if (auto updated = Repository<Presentation>(*m_db).update(p.value()); !updated.ok())
            qWarning("Auto-advance not saved: %s", qPrintable(updated.error().message));
    }
    const auto apply = [&](ItemView &v) {
        if (v.presentationId == presentationId)
            v.autoAdvance = seconds;
    };
    if (m_plan) {
        for (ItemView &v : m_plan->items)
            apply(v);
    }
    if (m_librarySong)
        apply(*m_librarySong);
    // On air now: it starts (or stops) at once.
    if (const ItemView *live = liveView(); live && live->presentationId == presentationId)
        restartAdvance(*live);
    emit stateChanged();
}

Content OperatorController::slideContent(const ItemView &view, int index) const
{
    const SlideView &slide = view.slides[std::size_t(index)];
    if (slide.mediaKind == QLatin1String("video")) {
        Content video = Content::video(slide.mediaPath, slide.mediaLoop, 1.0, slide.thumbnail);
        video.trimInMs = slide.trimInMs;
        video.trimOutMs = slide.trimOutMs;
        return video;
    }
    if (slide.mediaKind == QLatin1String("image"))
        return Content::image(slide.mediaPath);
    // A countdown slide shows its timer, counted by the renderer.
    const std::optional<QJsonObject> timer = slide.hasTimer && m_timerSource ? m_timerSource(slide.timerName) : std::nullopt;
    return !slide.elements.isEmpty() ? Content::composed(resolveTimers(slide.elements))
           : timer                   ? Content::timerContent(*timer, slide.text)
           : view.style              ? Content::styledText(slide.text, styleWithSize(*view.style, slide.fontSize), slide.footer)
                                     : Content::textContent(slide.text);
}

bool OperatorController::next()
{
    const int index = previewSlide();
    return index >= 0 && goLive(index);
}

bool OperatorController::previous()
{
    if (m_positionSlide <= 0)
        return false;
    if (positionIsSelected())
        return goLive(m_positionSlide - 1);
    // The next item is shown while another plan item is live: back to the
    // live item, a slide earlier (what the audience sees is what Previous
    // is about).
    if (m_positionItem >= 0 && item(m_positionItem)) {
        const int slide = m_positionSlide - 1;
        selectItem(m_positionItem);
        return goLive(slide);
    }
    return false;
}

// Picks the first candidate after the live slide, wrapping round, so pressing
// C again moves to the next chorus.
int OperatorController::firstSlideOfGroupFrom(const std::vector<int> &candidates) const
{
    if (candidates.empty())
        return -1;
    const int after = liveSlide();
    for (int candidate : candidates) {
        if (candidate > after)
            return candidate;
    }
    return candidates.front();
}

bool OperatorController::jumpToGroup(const QString &labelPrefix)
{
    const ItemView *view = selected();
    if (!view)
        return false;
    std::vector<int> starts; // first slide of each occurrence of the group
    QString previousGroup;
    for (int i = 0; i < int(view->slides.size()); ++i) {
        const QString &group = view->slides[std::size_t(i)].group;
        if (group != previousGroup && group.startsWith(labelPrefix, Qt::CaseInsensitive))
            starts.push_back(i);
        previousGroup = group;
    }
    return goLive(firstSlideOfGroupFrom(starts));
}

bool OperatorController::jumpToVerse(int number)
{
    const ItemView *view = selected();
    if (!view)
        return false;
    const QString wanted = QStringLiteral("verse %1").arg(number);
    std::vector<int> starts;
    QString previousGroup;
    for (int i = 0; i < int(view->slides.size()); ++i) {
        const QString &group = view->slides[std::size_t(i)].group;
        if (group != previousGroup && group.compare(wanted, Qt::CaseInsensitive) == 0)
            starts.push_back(i);
        previousGroup = group;
    }
    return goLive(firstSlideOfGroupFrom(starts));
}

bool OperatorController::jumpToSection(int number)
{
    // Sections are the groups in play order, numbered from 1.
    const ItemView *view = selected();
    if (!view || number < 1)
        return false;
    int section = 0;
    QString previousGroup;
    for (int i = 0; i < int(view->slides.size()); ++i) {
        const QString &group = view->slides[std::size_t(i)].group;
        if (i == 0 || group != previousGroup) {
            if (++section == number)
                return goLive(i);
        }
        previousGroup = group;
    }
    return false;
}

void OperatorController::clearLayer(const QString &name)
{
    const auto layer = layerFromName(name);
    if (!layer)
        return;
    // Clear Slide also stops full-screen media (videos sent as slides).
    if (*layer == Layer::Slide)
        m_show.setContents({{Layer::Slide, Content::empty()}, {Layer::Media, Content::empty()}});
    else
        m_show.clear(*layer);
}

void OperatorController::clearAll()
{
    m_show.clearAll();
}

void OperatorController::toggleLogo()
{
    m_show.toggleLogo();
}

void OperatorController::toggleBlack()
{
    m_show.toggleBlack();
}

bool OperatorController::handleKey(int key, int modifiers)
{
    const auto binding = m_keys.lookup(key, Qt::KeyboardModifiers(modifiers));
    const bool versePending = m_versePending;
    m_versePending = false;
    if (!binding)
        return false;

    switch (binding->action) {
    case KeyAction::OpenScripture: emit scriptureRequested(); return true;
    // Safety controls work in both modes: they only ever take things away.
    case KeyAction::ClearBackground: m_show.clear(Layer::Background); return true;
    case KeyAction::ClearSlide: clearLayer(QStringLiteral("slide")); return true;
    case KeyAction::ClearProps: m_show.clear(Layer::Props); return true;
    case KeyAction::ClearMessages: m_show.clear(Layer::Messages); return true;
    case KeyAction::ClearAll: m_show.clearAll(); return true;
    case KeyAction::Logo: m_show.toggleLogo(); return true;
    case KeyAction::Black: m_show.toggleBlack(); return true;
    default: break;
    }

    if (m_editMode)
        return false; // nothing else may reach the screen while editing

    switch (binding->action) {
    case KeyAction::NextSlide: next(); return true;
    case KeyAction::PreviousSlide: previous(); return true;
    case KeyAction::NextItem: nextItem(); return true;
    case KeyAction::PreviousItem: previousItem(); return true;
    case KeyAction::JumpToGroup: jumpToGroup(binding->group); return true;
    case KeyAction::VersePrefix:
        m_versePending = true;
        m_verseTimeout.start();
        return true;
    case KeyAction::JumpToSection:
        versePending ? jumpToVerse(binding->section) : jumpToSection(binding->section);
        return true;
    default: return false;
    }
}

} // namespace lumen::app

namespace lumen::app {

// ---- Plans (batch 11) ----

bool OperatorController::planFailed(const core::Status &status, const QString &what)
{
    if (status.ok())
        return false;
    m_planMessage = QStringLiteral("%1: %2").arg(what, status.error().message);
    emit stateChanged();
    return true;
}

void OperatorController::refreshSavedPlans()
{
    QList<QVariantMap> rows;
    if (m_db) {
        if (auto all = plans::list(*m_db); all.ok()) {
            for (const plans::PlanSummary &plan : all.value()) {
                rows << QVariantMap{{"planId", plan.id},
                                    {"name", plan.name},
                                    {"date", plan.date.isValid() ? QLocale().toString(plan.date, QLocale::ShortFormat) : QString()},
                                    {"items", plan.items},
                                    {"open", m_plan && m_plan->id == plan.id}};
            }
        }
    }
    m_savedPlansModel.setRows(rows);
}

// Reloads the open plan from the library after a change, keeping track of
// the item on screen (by its plan item id) and selecting `select`.
void OperatorController::reloadPlan(int select)
{
    if (!m_db || !m_plan)
        return;
    auto loaded = loadPlan(*m_db, m_plan->id);
    if (!loaded.ok()) {
        planFailed(loaded.error(), tr("The plan could not be loaded"));
        return;
    }
    m_plan = std::move(loaded).value();
    m_planModel.setRows(planRows());
    if (m_positionKey.startsWith(QLatin1String("plan:"))) {
        m_positionItem = -1;
        for (int i = 0; i < int(m_plan->items.size()); ++i) {
            if (keyOf(m_plan->items[size_t(i)]) == m_positionKey)
                m_positionItem = i;
        }
    }
    m_current = -1;
    selectItem(std::clamp(select, 0, std::max(0, int(m_plan->items.size()) - 1)));
    refreshSavedPlans();
    emit stateChanged();
}

bool OperatorController::openPlanById(const QString &planId)
{
    if (!m_db)
        return false;
    auto loaded = loadPlan(*m_db, planId);
    if (!loaded.ok())
        return !planFailed(loaded.error(), tr("The plan could not be opened"));
    // What is on screen stays there; the new plan starts with nothing live.
    setPlan(std::move(loaded).value());
    m_planMessage.clear();
    refreshSavedPlans();
    saveSession();
    emit stateChanged();
    return true;
}

bool OperatorController::newPlan(const QString &name)
{
    if (!m_db)
        return false;
    auto created = plans::create(*m_db, name.trimmed().isEmpty() ? tr("Sunday Service") : name, QDate::currentDate());
    if (!created.ok())
        return !planFailed(created.error(), tr("The plan could not be created"));
    return openPlanById(created.value().id);
}

bool OperatorController::openPlan(int row)
{
    return openPlanById(m_savedPlansModel.get(row).value(QStringLiteral("planId")).toString());
}

bool OperatorController::duplicatePlan()
{
    if (!m_db || !m_plan)
        return false;
    auto copy = plans::duplicate(*m_db, m_plan->id, tr("%1 (copy)").arg(m_plan->name), QDate::currentDate());
    if (!copy.ok())
        return !planFailed(copy.error(), tr("The plan could not be duplicated"));
    return openPlanById(copy.value().id);
}

bool OperatorController::renamePlan(const QString &name)
{
    if (!m_db || !m_plan)
        return false;
    if (planFailed(plans::rename(*m_db, m_plan->id, name, m_plan->date), tr("The plan could not be renamed")))
        return false;
    reloadPlan(m_current);
    return true;
}

bool OperatorController::deletePlan(int row)
{
    const QString id = m_savedPlansModel.get(row).value(QStringLiteral("planId")).toString();
    if (!m_db || id.isEmpty())
        return false;
    if (planFailed(plans::remove(*m_db, id), tr("The plan could not be deleted")))
        return false;
    if (m_plan && m_plan->id == id) {
        auto latest = loadLatestPlan(*m_db);
        setPlan(latest.ok() ? latest.value() : std::nullopt);
        saveSession();
    }
    refreshSavedPlans();
    emit stateChanged();
    return true;
}

bool OperatorController::deleteCurrentPlan()
{
    if (!m_plan)
        return false;
    for (int row = 0; row < m_savedPlansModel.count(); ++row) {
        if (m_savedPlansModel.get(row).value(QStringLiteral("planId")).toString() == m_plan->id)
            return deletePlan(row);
    }
    return false;
}

bool OperatorController::addHeader(const QString &text)
{
    if (!m_db)
        return false;
    if (!m_plan && !newPlan(QString()))
        return false;
    const int at = m_current >= 0 ? m_current + 1 : int(m_plan->items.size());
    auto header = plans::addHeader(*m_db, m_plan->id, at, text);
    if (!header.ok())
        return !planFailed(header.error(), tr("The header could not be added"));
    reloadPlan(at);
    return true;
}

bool OperatorController::removeItem(int index)
{
    if (!m_db || !m_plan)
        return false;
    if (planFailed(plans::removeItem(*m_db, m_plan->id, index), tr("The item could not be removed")))
        return false;
    reloadPlan(m_current > index ? m_current - 1 : m_current);
    return true;
}

bool OperatorController::addToPlanAt(const QString &source, int row, int at)
{
    bool added = false;
    if (source == QLatin1String("song"))
        added = addSongToPlan(row);
    else if (source == QLatin1String("media"))
        added = addMediaToPlan(row);
    else if (source == QLatin1String("deck"))
        added = addDeckToPlan(row);
    if (!added || !m_plan)
        return added;
    const int last = int(m_plan->items.size()) - 1;
    if (at >= 0 && at < last)
        moveItem(last, at);
    return true;
}

bool OperatorController::moveItem(int from, int to)
{
    if (!m_db || !m_plan || from == to)
        return false;
    if (planFailed(plans::moveItem(*m_db, m_plan->id, from, to), tr("The item could not be moved")))
        return false;
    // The selection follows the item it was on.
    int select = m_current;
    if (m_current == from)
        select = to;
    else if (from < m_current && to >= m_current)
        --select;
    else if (from > m_current && to <= m_current)
        ++select;
    reloadPlan(select);
    return true;
}

bool OperatorController::setItemNotes(int index, const QString &notes)
{
    if (!m_db || !m_plan)
        return false;
    if (planFailed(plans::setNotes(*m_db, m_plan->id, index, notes), tr("The notes could not be saved")))
        return false;
    reloadPlan(m_current);
    return true;
}

bool OperatorController::exportPlan(const QUrl &file)
{
    if (!m_db || !m_plan)
        return false;
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!path.endsWith(QLatin1String(".lumen"), Qt::CaseInsensitive))
        path += QStringLiteral(".lumen");
    if (planFailed(lumenfile::exportPlan(*m_db, m_plan->id, path), tr("Export failed")))
        return false;
    m_planMessage = tr("Exported to %1").arg(QFileInfo(path).fileName());
    emit stateChanged();
    return true;
}

bool OperatorController::importPlan(const QUrl &file)
{
    if (!m_db)
        return false;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const QString folder = m_importFolder.isEmpty() ? QFileInfo(path).absolutePath() : m_importFolder;
    auto report = lumenfile::importFile(*m_db, path, folder);
    if (!report.ok())
        return !planFailed(report.error(), tr("Import failed"));
    openPlanById(report.value().planId);
    m_planMessage = report.value().summary();
    if (!report.value().messages.isEmpty())
        m_planMessage += QStringLiteral(". ") + report.value().messages.join(QStringLiteral(". "));
    refreshMedia();
    refreshDecks();
    emit stateChanged();
    return true;
}

// ---- Backups and session ----

void OperatorController::attachBackups(Backups &backups, const QString &libraryPath, const QString &importFolder)
{
    m_backups = &backups;
    m_libraryPath = libraryPath;
    m_importFolder = importFolder;
    refreshBackups();
}

// What a library file holds, read without changing it: "12 items · 2 plans".
QString OperatorController::libraryContents(const QString &path)
{
    const QString name = QStringLiteral("lumen-peek-%1").arg(quintptr(&path));
    QString text;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            const auto count = [&q](const QString &table) {
                return q.exec(QStringLiteral("SELECT COUNT(*) FROM ") + table) && q.next() ? q.value(0).toInt() : 0;
            };
            text = tr("%n items", nullptr, count(QStringLiteral("presentations"))) + QStringLiteral(" · ")
                   + tr("%n plans", nullptr, count(QStringLiteral("plans")));
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return text;
}

QString OperatorController::currentLibraryContents() const
{
    return m_db ? libraryContents(m_db->path()) : QString();
}

void OperatorController::refreshBackups()
{
    QList<QVariantMap> rows;
    if (m_backups) {
        for (const Backups::Entry &entry : m_backups->list()) {
            rows << QVariantMap{{"path", entry.path},
                                {"made", QLocale().toString(entry.made.toLocalTime(), QLocale::ShortFormat)},
                                {"size", QLocale().formattedDataSize(entry.bytes)},
                                {"contents", libraryContents(entry.path)}};
        }
    }
    m_backupModel.setRows(rows);
    emit stateChanged();
}

QString OperatorController::lastBackup() const
{
    if (m_backupModel.rowCount() == 0)
        return tr("No backups yet");
    return tr("Last backup %1").arg(m_backupModel.get(0).value(QStringLiteral("made")).toString());
}

bool OperatorController::backUpNow()
{
    if (!m_backups)
        return false;
    auto made = m_backups->backUpNow();
    if (!made.ok())
        return !planFailed(made.error(), tr("Backup failed"));
    m_planMessage = tr("Backed up to %1").arg(QDir::toNativeSeparators(made.value().path));
    refreshBackups();
    return true;
}

bool OperatorController::restoreBackup(int row)
{
    const QString path = m_backupModel.get(row).value(QStringLiteral("path")).toString();
    if (!m_backups || path.isEmpty() || m_libraryPath.isEmpty())
        return false;
    if (planFailed(Backups::scheduleRestore(m_libraryPath, path), tr("Restore failed")))
        return false;
    m_planMessage = tr("Restarting to restore the backup…");
    emit stateChanged();
    emit restartRequested();
    return true;
}

void OperatorController::saveSession()
{
    if (m_libraryPath.isEmpty())
        return;
    // Beside the library, written whole or not at all.
    QSaveFile file(m_libraryPath + QStringLiteral(".session.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{"planId", m_plan ? m_plan->id : QString()}, {"item", m_current}}).toJson());
    file.commit();
}

bool OperatorController::restoreSession()
{
    if (!m_db || m_libraryPath.isEmpty())
        return false;
    QFile file(m_libraryPath + QStringLiteral(".session.json"));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject session = QJsonDocument::fromJson(file.readAll()).object();
    const QString planId = session.value(QStringLiteral("planId")).toString();
    if (planId.isEmpty())
        return false;
    auto loaded = loadPlan(*m_db, planId);
    if (!loaded.ok())
        return false;
    setPlan(std::move(loaded).value());
    selectItem(session.value(QStringLiteral("item")).toInt());
    refreshSavedPlans();
    return true;
}

} // namespace lumen::app

namespace lumen::app {

// ---- Timers and messages (batch 12) ----

QString OperatorController::liveTimerText() const
{
    const Content &content = m_show.scene().layer(Layer::Slide).content;
    if (content.kind != Content::Kind::Timer)
        return {};
    return core::show::timerText(content.timer, QDateTime::currentMSecsSinceEpoch());
}

QString OperatorController::previewTimerText() const
{
    const ItemView *view = selected();
    const int index = previewSlide();
    if (!view || index < 0 || !view->slides[size_t(index)].hasTimer || !m_timerSource)
        return {};
    const auto timer = m_timerSource(view->slides[size_t(index)].timerName);
    return timer ? core::show::timerText(*timer, QDateTime::currentMSecsSinceEpoch()) : QString();
}

QString OperatorController::messageShown() const
{
    const Content &content = m_show.scene().layer(Layer::Messages).content;
    return content.kind == Content::Kind::Text ? content.text : QString();
}

bool OperatorController::showMessage(const QString &text, bool ticker)
{
    const QString line = text.simplified();
    if (line.isEmpty())
        return false;
    Content content = Content::textContent(line);
    if (ticker)
        content.style = QJsonObject{{"ticker", true}};
    m_show.setContent(Layer::Messages, content);
    m_recentMessages.removeAll(line);
    m_recentMessages.prepend(line);
    while (m_recentMessages.size() > 8)
        m_recentMessages.removeLast();
    emit stateChanged();
    return true;
}

void OperatorController::hideMessage()
{
    if (!m_show.scene().layer(Layer::Messages).content.isEmpty())
        m_show.setContent(Layer::Messages, Content::empty());
    emit stateChanged();
}

} // namespace lumen::app

namespace lumen::app {

// ---- Stage display (batch 13) ----

QJsonObject OperatorController::stageState() const
{
    QJsonObject state;
    // On air: the item and slide last sent live, while something shows.
    const ItemView *live = m_onAir ? item(m_positionItem) : nullptr;
    if (live && keyOf(*live) != m_positionKey)
        live = nullptr;
    const bool slideShowing = !m_show.scene().layer(Layer::Slide).content.isEmpty()
                              || !m_show.scene().layer(Layer::Media).content.isEmpty();
    if (live && slideShowing && m_positionSlide >= 0 && m_positionSlide < int(live->slides.size())) {
        const SlideView &slide = live->slides[size_t(m_positionSlide)];
        state.insert("item", live->title);
        QStringList detail;
        if (!live->key.isEmpty() && live->transpose != 0)
            detail << tr("Key of %1 (written in %2)").arg(core::songs::transposeKey(live->key, live->transpose), live->key);
        else if (!live->key.isEmpty())
            detail << tr("Key of %1").arg(live->key);
        if (!live->notes.isEmpty())
            detail << live->notes;
        state.insert("detail", detail.join(QStringLiteral(" · ")));
        state.insert("group", slide.group);
        QString now = slide.text;
        if (now.isEmpty() && slide.mediaKind == QLatin1String("video"))
            now = tr("[Video] %1").arg(live->title);
        else if (now.isEmpty() && slide.mediaKind == QLatin1String("image"))
            now = tr("[Picture] %1").arg(live->title);
        state.insert("now", now);
        // For the band (the Chords layout): the same lines with chords.
        if (!slide.chords.isEmpty())
            state.insert("nowChords", chordsForStage(slide.chords, live->transpose, live->key));
        // The arrangement, with the part on air marked: each change of group
        // in play order is the next chip.
        int chip = 0;
        for (int i = 1; i <= m_positionSlide; ++i)
            chip += live->slides[size_t(i)].group != live->slides[size_t(i - 1)].group ? 1 : 0;
        QJsonArray chips;
        for (int i = 0; i < int(live->chips.size()); ++i)
            chips.append(QJsonObject{{"label", live->chips[size_t(i)].label}, {"current", i == chip}});
        if (chips.size() > 1)
            state.insert("arrangement", chips);
    }
    // Next: what Space sends.
    if (const ItemView *view = selected(); view && previewSlide() >= 0) {
        const SlideView &next = view->slides[size_t(previewSlide())];
        state.insert("next", next.text.isEmpty() && !next.mediaKind.isEmpty() ? view->title : next.text);
        state.insert("nextGroup", next.group);
        if (!next.chords.isEmpty())
            state.insert("nextChords", chordsForStage(next.chords, view->transpose, view->key));
    }
    return state;
}

} // namespace lumen::app

namespace lumen::app {

// ---- Composed slides (batch 16) ----

// Timer elements name their timer; on air they carry its spec, so the
// renderer counts them.
QJsonArray OperatorController::resolveTimers(const QJsonArray &elements) const
{
    QJsonArray out;
    for (const QJsonValue &value : elements) {
        QJsonObject element = value.toObject();
        if (element.value("type").toString() == QLatin1String("timer") && m_timerSource) {
            if (const auto spec = m_timerSource(element.value("timerName").toString()))
                element.insert("timer", *spec);
        }
        out.append(element);
    }
    return out;
}

QVariantList OperatorController::previewElements() const
{
    const ItemView *view = selected();
    const int index = previewSlide();
    if (!view || index < 0)
        return {};
    return resolveTimers(view->slides[size_t(index)].elements).toVariantList();
}

QVariantList OperatorController::liveElements() const
{
    const Content &content = m_show.scene().layer(Layer::Slide).content;
    return content.kind == Content::Kind::Composed ? content.elements.toVariantList() : QVariantList();
}

} // namespace lumen::app

namespace lumen::app {

void OperatorController::refreshAfterEdit()
{
    if (m_librarySong && m_db) {
        if (auto view = loadPresentationView(*m_db, m_librarySong->presentationId); view.ok())
            m_librarySong = std::move(view).value();
    }
    const bool librarySong = m_current == libraryItem;
    if (m_plan)
        reloadPlan(librarySong ? 0 : m_current);
    if (librarySong)
        m_current = libraryItem; // the song shown from the library stays shown
    showSelected();
    refreshDecks();
    refreshMedia();
    emit stateChanged();
}

} // namespace lumen::app

namespace lumen::app {

void OperatorController::refreshLibrary()
{
    searchSongs(m_search);
    refreshSavedPlans();
    refreshMedia();
    refreshDecks();
    emit stateChanged();
}

} // namespace lumen::app

namespace lumen::app {

// ---- Timecode cues (batch 20) ----

bool OperatorController::playCues(const QJsonObject &timeline, qint64 offsetMs)
{
    // timeline: the item's {"audio", "cues": [{"ms", "slide"}]}, or with
    // "source": "mtc", "mtcDevice" and "mtcStart" to follow MIDI timecode.
    const ItemView *view = selected();
    if (m_editMode || !view)
        return false;
    QJsonArray cues;
    for (const QJsonValue &v : timeline.value("cues").toArray()) {
        const int slide = v.toObject().value("slide").toInt(-1);
        if (slide < 0 || slide >= int(view->slides.size()))
            continue;
        cues.append(QJsonObject{{"ms", v.toObject().value("ms").toInteger()},
                                {"content", core::show::contentToJson(slideContent(*view, slide))}});
    }
    if (cues.isEmpty())
        return false;
    static int runs = 0;
    QJsonObject live{{"id", QStringLiteral("%1-%2").arg(view->presentationId).arg(++runs)},
                     {"audio", timeline.value("audio").toString()},
                     {"playing", true},
                     {"offsetMs", offsetMs},
                     {"delayMs", timeline.value("delayMs").toInteger()},
                     {"cues", cues}};
    const bool timecode = timeline.value("source").toString() == QLatin1String("mtc");
    if (timecode) {
        live.insert("source", QStringLiteral("mtc"));
        live.insert("mtcDevice", timeline.value("mtcDevice").toString());
        live.insert("mtcStart", timeline.value("mtcStart").toString());
        live.remove("audio");
    }
    std::vector<std::pair<Layer, Content>> changes{{Layer::Slide, Content::cued(live)}};
    if (!m_show.scene().layer(Layer::Media).content.isEmpty())
        changes.push_back({Layer::Media, Content::empty()});
    if (const auto background = backgroundContent(view->background); background && !m_manualBackground)
        changes.push_back({Layer::Background, *background});
    m_positionItem = m_current;
    m_positionKey = keyOf(*view);
    m_positionSlide = -1;
    m_positionLabel = timecode ? tr("Following MIDI timecode") : tr("Following the backing track");
    m_onAir = true;
    m_show.setContents(changes);
    return true;
}

void OperatorController::stopCues()
{
    if (cuesPlaying())
        m_show.setContent(Layer::Slide, Content::empty());
}

bool OperatorController::cuesPlaying() const
{
    return m_show.scene().layer(Layer::Slide).content.kind == Content::Kind::Cued;
}

} // namespace lumen::app
