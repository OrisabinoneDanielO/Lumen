#include "operatorcontroller.h"

#include "lumenfile.h"
#include "planstore.h"

#include "themes.h"

#include "lumen/core/data/repository.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QJsonArray>
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
    searchSongs(m_search);
    refreshSavedPlans();
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
                                {"loop", row.loop}};
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

bool OperatorController::useAsBackground(int row)
{
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
    if (!item(index) || index == m_current)
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
    // Edit mode can never change what is on screen.
    const ItemView *view = selected();
    if (m_editMode || !view || index < 0 || index >= int(view->slides.size()))
        return false;
    m_positionItem = m_current;
    m_positionKey = keyOf(*view);
    m_positionSlide = index;
    m_positionLabel = tr("%1 · slide %2").arg(view->slides[std::size_t(index)].group).arg(index + 1);
    m_onAir = true;
    const SlideView &slide = view->slides[std::size_t(index)];

    std::vector<std::pair<Layer, Content>> changes;
    if (!slide.mediaKind.isEmpty()) {
        // A video or picture fills the Media layer; the slide text goes.
        const Content media = slide.mediaKind == QLatin1String("video")
                                  ? Content::video(slide.mediaPath, false, 1.0, slide.thumbnail)
                                  : Content::image(slide.mediaPath);
        changes = {{Layer::Media, media}, {Layer::Slide, Content::empty()}};
    } else {
        // A countdown slide shows its timer, counted by the renderer.
        const std::optional<QJsonObject> timer =
            slide.hasTimer && m_timerSource ? m_timerSource(slide.timerName) : std::nullopt;
        const Content content = timer ? Content::timerContent(*timer, slide.text)
                                : view->style
                                    ? Content::styledText(slide.text, styleWithSize(*view->style, slide.fontSize), slide.footer)
                                    : Content::textContent(slide.text);
        changes = {{Layer::Slide, content}};
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
    return true;
}

bool OperatorController::next()
{
    const int index = previewSlide();
    return index >= 0 && goLive(index);
}

bool OperatorController::previous()
{
    if (!positionIsSelected() || m_positionSlide <= 0)
        return false;
    return goLive(m_positionSlide - 1);
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

void OperatorController::refreshBackups()
{
    QList<QVariantMap> rows;
    if (m_backups) {
        for (const Backups::Entry &entry : m_backups->list()) {
            rows << QVariantMap{{"path", entry.path},
                                {"made", QLocale().toString(entry.made.toLocalTime(), QLocale::ShortFormat)},
                                {"size", QLocale().formattedDataSize(entry.bytes)}};
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

bool OperatorController::showMessage(const QString &text)
{
    const QString line = text.simplified();
    if (line.isEmpty())
        return false;
    m_show.setContent(Layer::Messages, Content::textContent(line));
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
        if (!live->key.isEmpty())
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
    }
    return state;
}

} // namespace lumen::app
