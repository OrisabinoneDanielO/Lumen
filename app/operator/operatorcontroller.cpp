#include "operatorcontroller.h"

#include "themes.h"

#include "lumen/core/data/repository.h"

#include <QFileInfo>
#include <QLocale>

namespace lumen::app {

using namespace core::show;

OperatorController::OperatorController(ShowController &show, bool editAvailable, QObject *parent)
    : QObject(parent), m_show(show), m_editAvailable(editAvailable)
{
    connect(&m_show, &ShowController::sceneChanged, this, [this] {
        // Something else (e.g. Clear Slide) took the slide off air.
        if (m_show.scene().layer(Layer::Slide).content.isEmpty())
            m_onAir = false;
        emit stateChanged();
    });
    m_verseTimeout.setSingleShot(true);
    m_verseTimeout.setInterval(1500);
    connect(&m_verseTimeout, &QTimer::timeout, this, [this] { m_versePending = false; });
}

void OperatorController::setPlan(std::optional<PlanView> plan)
{
    m_plan = std::move(plan);
    QList<QVariantMap> rows;
    if (m_plan) {
        for (const ItemView &item : m_plan->items)
            rows << QVariantMap{{"kind", item.kind}, {"title", item.title}, {"subtitle", item.subtitle}};
    }
    m_planModel.setRows(rows);
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

    QList<QVariantMap> rows;
    for (const ItemView &item : m_plan->items)
        rows << QVariantMap{{"kind", item.kind}, {"title", item.title}, {"subtitle", item.subtitle}};
    m_planModel.setRows(rows);
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
    return content.kind == Content::Kind::Text ? content.text : QString();
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
        case Content::Kind::Text:
            value = (layer == Layer::Slide && m_onAir) ? liveLabel() : content.text.section(u'\n', 0, 0);
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
        for (const SlideView &slide : view->slides)
            rows << QVariantMap{{"group", slide.group}, {"colour", slide.colour}, {"text", slide.text}};
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

    const Content content = view->style
                                ? Content::styledText(slide.text, styleWithSize(*view->style, slide.fontSize), slide.footer)
                                : Content::textContent(slide.text);
    // A themed item brings its background with it, in the same change.
    std::vector<std::pair<Layer, Content>> changes{{Layer::Slide, content}};
    if (const auto background = backgroundContent(view->background))
        changes.push_back({Layer::Background, *background});

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
    if (const auto layer = layerFromName(name))
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
    case KeyAction::ClearSlide: m_show.clear(Layer::Slide); return true;
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
