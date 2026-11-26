#include "operatorcontroller.h"

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
                            {"copyright", song.copyright}, {"key", song.key}};
    }
    m_songModel.setRows(rows);
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

int OperatorController::liveSlide() const
{
    return m_onAir && m_positionItem == m_current ? m_positionSlide : -1;
}

int OperatorController::previewSlide() const
{
    const ItemView *view = selected();
    if (!view || view->slides.empty())
        return -1;
    if (m_positionItem == m_current && m_positionSlide >= 0) {
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

QString OperatorController::liveLabel() const
{
    const ItemView *view = item(m_positionItem);
    if (!m_onAir || !view || m_positionSlide < 0)
        return tr("Clear");
    return tr("%1 · slide %2").arg(view->slides[std::size_t(m_positionSlide)].group).arg(m_positionSlide + 1);
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
    m_selected = 0;
    QList<QVariantMap> rows;
    for (const SlideView &slide : selected()->slides)
        rows << QVariantMap{{"group", slide.group}, {"colour", slide.colour}, {"text", slide.text}};
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
    m_positionSlide = index;
    m_onAir = true;
    const Content content = Content::textContent(view->slides[std::size_t(index)].text);
    if (m_show.scene().layer(Layer::Slide).content == content)
        emit stateChanged(); // same text, new position (e.g. a repeated chorus)
    else
        m_show.setContent(Layer::Slide, content);
    return true;
}

bool OperatorController::next()
{
    const int index = previewSlide();
    return index >= 0 && goLive(index);
}

bool OperatorController::previous()
{
    if (m_positionItem != m_current || m_positionSlide <= 0)
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
