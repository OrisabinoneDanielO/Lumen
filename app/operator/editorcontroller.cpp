#include "editorcontroller.h"

#include "lumen/core/songs/chords.h"

#include "medialibrary.h"
#include "operatorcontroller.h"
#include "servicedata.h"

#include "lumen/core/data/repository.h"
#include "lumen/core/songs/songimport.h"
#include "lumen/core/songs/songlibrary.h"

#include <QFileInfo>
#include <QHash>

namespace lumen::app {

using namespace core;
using namespace core::data;

namespace {

const QStringList typeNames{"text", "shape", "image", "video", "clock", "timer"};

ElementType typeFrom(const QString &name)
{
    if (name == QLatin1String("shape"))
        return ElementType::Shape;
    if (name == QLatin1String("image"))
        return ElementType::Image;
    if (name == QLatin1String("video"))
        return ElementType::Video;
    if (name == QLatin1String("clock"))
        return ElementType::Clock;
    if (name == QLatin1String("timer"))
        return ElementType::Timer;
    return ElementType::Text;
}

Error invalid(const QString &message)
{
    return Error{Error::Code::Invalid, message};
}

} // namespace

EditorController::EditorController(OperatorController &op, QObject *parent) : QObject(parent), m_op(op) {}

void EditorController::attach(Database &db, songs::SongLibrary *songs)
{
    m_db = &db;
    m_songs = songs;
    m_themes.clear();
    QList<QVariantMap> rows;
    if (auto all = Repository<Theme>(db).list(); all.ok()) {
        m_themes = all.value();
        for (const Theme &t : m_themes)
            rows << QVariantMap{{"name", t.name}};
    }
    m_themeModel.setRows(rows);
}

// ---- opening -------------------------------------------------------------

bool EditorController::openPresentation(const QString &presentationId)
{
    if (!m_db || presentationId.isEmpty())
        return false;
    auto presentation = Repository<Presentation>(*m_db).get(presentationId);
    if (!presentation.ok())
        return false;
    load(presentationId);
    return true;
}

bool EditorController::openSelected()
{
    return openPresentation(m_op.selectedPresentationId());
}

void EditorController::load(const QString &presentationId)
{
    m_presentation = Repository<Presentation>(*m_db).get(presentationId).value();
    m_draft.clear();
    std::vector<std::pair<QString, QStringList>> groupTexts;
    auto groups = Repository<Group>(*m_db).listChildren(presentationId);
    for (const Group &group : groups.ok() ? groups.value() : std::vector<Group>{}) {
        QStringList texts;
        auto slides = Repository<Slide>(*m_db).listChildren(group.id);
        for (const Slide &slide : slides.ok() ? slides.value() : std::vector<Slide>{}) {
            DraftSlide draft{group.label, slide.transition, slide.notes, slide.chords, {}};
            auto elements = Repository<Element>(*m_db).listChildren(slide.id);
            std::vector<Element> ordered = elements.ok() ? elements.value() : std::vector<Element>{};
            std::stable_sort(ordered.begin(), ordered.end(), [](const Element &a, const Element &b) { return a.zOrder < b.zOrder; });
            QStringList slideText;
            for (const Element &e : ordered) {
                DraftElement d{e.type, e.frame, e.style, e.content, {}, e.mediaAssetId};
                if (!e.mediaAssetId.isEmpty()) {
                    if (auto asset = Repository<MediaAsset>(*m_db).get(e.mediaAssetId); asset.ok())
                        d.mediaPath = asset.value().filePath;
                }
                if (e.type == ElementType::Text)
                    slideText << e.content;
                draft.elements.push_back(std::move(d));
            }
            // Songs: the lyrics show their chords inline ("[G]Amazing").
            texts << (slide.chords.contains(u'[') ? slide.chords : slideText.join(u'\n'));
            m_draft.push_back(std::move(draft));
        }
        groupTexts.push_back({group.label, texts});
    }
    m_lyrics = lyricsOf(groupTexts);
    m_lyricsChanged = false;
    // The stored play order; set by hand if it is not the groups as written
    // (a chorus repeated, a verse left out).
    m_arrangement.clear();
    m_arrangementChanged = false;
    if (isSong()) {
        QHash<QString, QString> labels;
        QStringList written;
        for (const Group &group : groups.ok() ? groups.value() : std::vector<Group>{}) {
            labels.insert(group.id, group.label);
            written << group.label;
        }
        auto arrangements = Repository<Arrangement>(*m_db).listChildren(presentationId);
        if (arrangements.ok() && !arrangements.value().empty()) {
            const auto &list = arrangements.value();
            const auto chosen = std::find_if(list.begin(), list.end(), [](const Arrangement &a) { return a.isDefault; });
            for (const QString &id : (chosen != list.end() ? *chosen : list.front()).groupIds) {
                if (labels.contains(id))
                    m_arrangement << labels.value(id);
            }
        }
        m_arrangementCustom = !m_arrangement.isEmpty() && m_arrangement != written;
        if (!m_arrangementCustom)
            m_arrangement.clear();
    } else {
        m_arrangementCustom = false;
    }
    m_dirty = false;
    m_slide = 0;
    m_element = -1;
    m_message.clear();
    refresh();
}

bool EditorController::newPresentation(const QString &title)
{
    if (!m_db)
        return false;
    QString id;
    const Status made = m_db->inTransaction([&]() -> Status {
        QString libraryId;
        if (auto libraries = Repository<Library>(*m_db).list(); libraries.ok() && !libraries.value().empty())
            libraryId = libraries.value().front().id;
        else if (auto created = Repository<Library>(*m_db).create({.name = "Main library"}); created.ok())
            libraryId = created.value().id;
        auto presentation = Repository<Presentation>(*m_db).create(
            {.libraryId = libraryId, .kind = PresentationKind::Slides,
             .title = title.trimmed().isEmpty() ? tr("New slides") : title.trimmed(),
             .metadata = {{"subtitle", tr("Made in Lumen")}}});
        if (!presentation.ok())
            return presentation.error();
        id = presentation.value().id;
        auto group = Repository<Group>(*m_db).create({.presentationId = id, .label = QStringLiteral("Slides")});
        if (!group.ok())
            return group.error();
        auto slide = Repository<Slide>(*m_db).create({.groupId = group.value().id});
        return slide.ok() ? success() : Status(slide.error());
    });
    if (!made.ok())
        return false;
    // Into the plan, selected, so the operator window and the editor agree.
    if (m_op.addPresentationToPlan(id))
        m_op.selectItem(m_op.plan()->property("count").toInt() - 1);
    return openPresentation(id);
}

// ---- the draft -------------------------------------------------------------

EditorController::DraftSlide *EditorController::current()
{
    return m_slide >= 0 && m_slide < int(m_draft.size()) ? &m_draft[size_t(m_slide)] : nullptr;
}

EditorController::DraftElement *EditorController::currentElement()
{
    DraftSlide *slide = current();
    return slide && m_element >= 0 && m_element < int(slide->elements.size()) ? &slide->elements[size_t(m_element)] : nullptr;
}

void EditorController::touch()
{
    m_dirty = true;
    refresh();
}

QJsonArray EditorController::elementsJson(const DraftSlide &slide) const
{
    QJsonArray out;
    for (const DraftElement &e : slide.elements) {
        QJsonObject json{{"type", elementTypeName(e.type)},
                         {"frame", QJsonArray{e.frame.x(), e.frame.y(), e.frame.width(), e.frame.height()}},
                         {"style", e.style}};
        if (e.type == ElementType::Text)
            json.insert("text", e.content);
        if (e.type == ElementType::Timer)
            json.insert("timerName", e.content);
        if (!e.mediaPath.isEmpty())
            json.insert(e.type == ElementType::Video ? "video" : "image", QUrl::fromLocalFile(e.mediaPath).toString());
        out.append(json);
    }
    return m_op.resolveTimers(out);
}

void EditorController::refresh()
{
    QList<QVariantMap> rows;
    for (const DraftSlide &slide : m_draft) {
        QStringList text;
        for (const DraftElement &e : slide.elements) {
            if (e.type == ElementType::Text)
                text << e.content;
        }
        rows << QVariantMap{{"group", slide.group}, {"text", text.join(u'\n')}, {"elements", elementsJson(slide).toVariantList()}};
    }
    m_slideModel.setRows(rows);
    emit changed();
}

QVariantList EditorController::elements() const
{
    return m_slide >= 0 && m_slide < int(m_draft.size()) ? elementsJson(m_draft[size_t(m_slide)]).toVariantList() : QVariantList();
}

QVariantMap EditorController::selected() const
{
    if (m_slide < 0 || m_slide >= int(m_draft.size()) || m_element < 0 || m_element >= int(m_draft[size_t(m_slide)].elements.size()))
        return {};
    const DraftElement &e = m_draft[size_t(m_slide)].elements[size_t(m_element)];
    return {{"type", elementTypeName(e.type)},
            {"x", e.frame.x()},
            {"y", e.frame.y()},
            {"width", e.frame.width()},
            {"height", e.frame.height()},
            {"style", e.style.toVariantMap()},
            {"text", e.type == ElementType::Text ? e.content : QString()},
            {"timerName", e.type == ElementType::Timer ? e.content : QString()},
            {"media", QFileInfo(e.mediaPath).fileName()}};
}

QString EditorController::transition() const
{
    if (m_slide < 0 || m_slide >= int(m_draft.size()))
        return QStringLiteral("theme");
    const QString kind = m_draft[size_t(m_slide)].transition.value("kind").toString();
    return kind.isEmpty() ? QStringLiteral("theme") : kind;
}

int EditorController::theme() const
{
    for (int i = 0; i < int(m_themes.size()); ++i) {
        if (m_themes[size_t(i)].id == m_presentation.themeId)
            return i;
    }
    return -1;
}

void EditorController::setTitle(const QString &title)
{
    if (!isOpen() || title.trimmed().isEmpty() || title == m_presentation.title)
        return;
    m_presentation.title = title.trimmed();
    touch();
}

void EditorController::setLyrics(const QString &lyrics)
{
    if (!isOpen() || !isSong() || lyrics == m_lyrics)
        return;
    m_lyrics = lyrics;
    m_lyricsChanged = true;
    m_dirty = true;
    emit changed();
}

QStringList EditorController::writtenOrder() const
{
    auto parsed = songs::parseTextSong(toSongText(m_lyrics), m_presentation.title);
    return parsed.ok() ? parsed.value().order : QStringList();
}

QStringList EditorController::groupLabels() const
{
    QStringList labels;
    if (auto parsed = songs::parseTextSong(toSongText(m_lyrics), m_presentation.title); parsed.ok()) {
        for (const songs::ParsedSection &section : parsed.value().sections)
            labels << section.label;
    }
    return labels;
}

QStringList EditorController::arrangement() const
{
    if (!isSong())
        return {};
    if (!m_arrangementCustom)
        return writtenOrder();
    // Groups since renamed or removed from the lyrics drop out.
    const QStringList known = groupLabels();
    QStringList order;
    for (const QString &label : m_arrangement) {
        if (known.contains(label))
            order << label;
    }
    return order;
}

void EditorController::arrangementEdited()
{
    m_arrangementChanged = true;
    m_dirty = true;
    emit changed();
}

void EditorController::addToArrangement(const QString &label)
{
    if (!isOpen() || !isSong() || !groupLabels().contains(label))
        return;
    m_arrangement = arrangement();
    m_arrangementCustom = true;
    m_arrangement << label;
    arrangementEdited();
}

void EditorController::removeFromArrangement(int index)
{
    QStringList order = arrangement();
    if (!isOpen() || index < 0 || index >= order.size() || order.size() == 1)
        return; // a song keeps at least one group
    order.removeAt(index);
    m_arrangement = order;
    m_arrangementCustom = true;
    arrangementEdited();
}

void EditorController::moveInArrangement(int from, int to)
{
    QStringList order = arrangement();
    if (!isOpen() || from < 0 || from >= order.size() || to < 0 || to >= order.size() || from == to)
        return;
    order.move(from, to);
    m_arrangement = order;
    m_arrangementCustom = true;
    arrangementEdited();
}

void EditorController::resetArrangement()
{
    if (!isOpen() || !m_arrangementCustom)
        return;
    m_arrangement.clear();
    m_arrangementCustom = false;
    arrangementEdited();
}

void EditorController::autoSplit()
{
    if (!isOpen() || !isSong())
        return;
    const int perSlide = qMax(1, m_songs ? m_songs->linesPerSlide() : 2);
    // Each group: its header (if any) and every line, breaks ignored.
    std::vector<std::pair<QString, QStringList>> groups{{QString(), {}}};
    for (const QString &line : m_lyrics.split(u'\n')) {
        const QString t = line.trimmed();
        // A header is one [label] alone, not a chord ("[G]Amazing grace [D]").
        if (t.startsWith(u'[') && t.endsWith(u']') && t.count(u'[') == 1 && !songs::isChord(t.mid(1, t.size() - 2)))
            groups.push_back({t, {}});
        else if (!t.isEmpty() && t != QLatin1String("---"))
            groups.back().second << t;
    }
    QStringList out;
    for (const auto &[header, lines] : groups) {
        if (header.isEmpty() && lines.isEmpty())
            continue;
        QStringList slides;
        for (qsizetype i = 0; i < lines.size(); i += perSlide)
            slides << lines.mid(i, perSlide).join(u'\n');
        out << (header.isEmpty() ? slides.join(QStringLiteral("\n\n"))
                                 : slides.isEmpty() ? header : header + u'\n' + slides.join(QStringLiteral("\n\n")));
    }
    setLyrics(out.join(QStringLiteral("\n\n")));
}

void EditorController::selectSlide(int index)
{
    if (index < 0 || index >= int(m_draft.size()) || index == m_slide)
        return;
    m_slide = index;
    m_element = -1;
    emit changed();
}

void EditorController::selectElement(int index)
{
    const DraftSlide *slide = m_slide >= 0 && m_slide < int(m_draft.size()) ? &m_draft[size_t(m_slide)] : nullptr;
    const int next = slide && index >= 0 && index < int(slide->elements.size()) ? index : -1;
    if (next == m_element)
        return;
    m_element = next;
    emit changed();
}

bool EditorController::addElement(const QString &type, const QUrl &media)
{
    DraftSlide *slide = current();
    if (!canInsert() || !slide || !typeNames.contains(type))
        return false;
    DraftElement e;
    e.type = typeFrom(type);
    const QString path = media.isLocalFile() ? media.toLocalFile() : media.toString();
    switch (e.type) {
    case ElementType::Text:
        e.frame = QRectF(192, 384, 1536, 312);
        e.style = {{"font", "Noto Sans"}, {"size", 72}, {"colour", "#FFFFFF"}, {"align", "center"},
                   {"valign", "center"}, {"bold", true}, {"shadow", true}, {"fit", true}};
        e.content = tr("Text");
        break;
    case ElementType::Shape:
        e.frame = QRectF(660, 340, 600, 400);
        e.style = {{"fill", "#F08A24"}, {"shape", "rect"}, {"radius", 24}};
        break;
    case ElementType::Image:
    case ElementType::Video:
        if (!QFileInfo(path).isFile()) {
            m_message = tr("Choose a picture or video file");
            emit changed();
            return false;
        }
        e.frame = e.type == ElementType::Image ? QRectF(560, 240, 800, 600) : QRectF(480, 180, 960, 540);
        e.style = e.type == ElementType::Image ? QJsonObject{{"fit", "contain"}}
                                               : QJsonObject{{"fit", "contain"}, {"loop", true}, {"muted", true}};
        e.mediaPath = QFileInfo(path).absoluteFilePath();
        break;
    case ElementType::Clock:
        e.frame = QRectF(1440, 60, 420, 120);
        e.style = {{"font", "Noto Sans"}, {"size", 72}, {"colour", "#FFFFFF"}, {"bold", true}, {"align", "right"},
                   {"format", "hh:mm"}, {"fit", false}};
        break;
    case ElementType::Timer:
        e.frame = QRectF(560, 420, 800, 240);
        e.style = {{"font", "Noto Sans"}, {"size", 160}, {"colour", "#FFFFFF"}, {"bold", true}, {"align", "center"},
                   {"fit", true}};
        break;
    }
    slide->elements.push_back(e);
    m_element = int(slide->elements.size()) - 1;
    m_message.clear();
    touch();
    return true;
}

bool EditorController::removeElement()
{
    DraftSlide *slide = current();
    if (!slide || !currentElement())
        return false;
    slide->elements.erase(slide->elements.begin() + m_element);
    m_element = -1;
    touch();
    return true;
}

void EditorController::setFrame(int index, qreal x, qreal y, qreal width, qreal height)
{
    DraftSlide *slide = current();
    if (!slide || index < 0 || index >= int(slide->elements.size()))
        return;
    // Kept on the slide, at least a little visible box.
    width = std::clamp(width, 16.0, 1920.0);
    height = std::clamp(height, 16.0, 1080.0);
    x = std::clamp(x, -width + 16, 1920.0 - 16);
    y = std::clamp(y, -height + 16, 1080.0 - 16);
    const QRectF frame(std::round(x), std::round(y), std::round(width), std::round(height));
    if (slide->elements[size_t(index)].frame == frame)
        return;
    slide->elements[size_t(index)].frame = frame;
    m_element = index;
    touch();
}

void EditorController::setText(const QString &text)
{
    DraftElement *e = currentElement();
    if (!e || e->type != ElementType::Text || e->content == text)
        return;
    e->content = text;
    touch();
}

void EditorController::setStyle(const QString &key, const QVariant &value)
{
    DraftElement *e = currentElement();
    if (!e || key.isEmpty())
        return;
    const QJsonValue json = QJsonValue::fromVariant(value);
    if (e->style.value(key) == json)
        return;
    e->style.insert(key, json);
    touch();
}

void EditorController::setTimerName(const QString &name)
{
    DraftElement *e = currentElement();
    if (!e || e->type != ElementType::Timer || e->content == name)
        return;
    e->content = name;
    touch();
}

void EditorController::raise()
{
    DraftSlide *slide = current();
    if (!slide || !currentElement() || m_element + 1 >= int(slide->elements.size()))
        return;
    std::swap(slide->elements[size_t(m_element)], slide->elements[size_t(m_element + 1)]);
    ++m_element;
    touch();
}

void EditorController::lower()
{
    DraftSlide *slide = current();
    if (!slide || !currentElement() || m_element == 0)
        return;
    std::swap(slide->elements[size_t(m_element)], slide->elements[size_t(m_element - 1)]);
    --m_element;
    touch();
}

void EditorController::addSlide()
{
    if (!canInsert())
        return;
    const QString group = current() ? current()->group : QStringLiteral("Slides");
    m_draft.insert(m_draft.begin() + std::min(m_slide + 1, int(m_draft.size())), DraftSlide{group, {}, {}, {}, {}});
    m_slide = std::min(m_slide + 1, int(m_draft.size()) - 1);
    m_element = -1;
    touch();
}

void EditorController::duplicateSlide()
{
    if (!canInsert() || !current())
        return;
    const DraftSlide copy = *current();
    m_draft.insert(m_draft.begin() + m_slide + 1, copy);
    ++m_slide;
    m_element = -1;
    touch();
}

bool EditorController::removeSlide()
{
    if (!canInsert() || !current() || m_draft.size() <= 1)
        return false;
    m_draft.erase(m_draft.begin() + m_slide);
    m_slide = std::min(m_slide, int(m_draft.size()) - 1);
    m_element = -1;
    touch();
    return true;
}

void EditorController::setTransition(const QString &kind)
{
    DraftSlide *slide = current();
    if (!slide)
        return;
    QJsonObject transition;
    if (kind == QLatin1String("cut"))
        transition = {{"kind", "cut"}};
    else if (kind == QLatin1String("fade"))
        transition = {{"kind", "fade"}, {"durationMs", 500}};
    if (transition == slide->transition)
        return;
    slide->transition = transition;
    touch();
}

bool EditorController::setBackground(const QUrl &media)
{
    DraftSlide *slide = current();
    const QString path = media.isLocalFile() ? media.toLocalFile() : media.toString();
    if (!canInsert() || !slide || !QFileInfo(path).isFile())
        return false;
    // One background per slide, at the bottom.
    std::erase_if(slide->elements, [](const DraftElement &e) { return e.style.value("role").toString() == QLatin1String("background"); });
    DraftElement background;
    background.type = MediaLibrary::isVideo(path) ? ElementType::Video : ElementType::Image;
    background.frame = QRectF(0, 0, 1920, 1080);
    background.style = {{"role", "background"}, {"fit", "cover"}, {"loop", true}, {"muted", true}};
    background.mediaPath = QFileInfo(path).absoluteFilePath();
    slide->elements.insert(slide->elements.begin(), background);
    m_element = 0;
    touch();
    return true;
}

void EditorController::setTheme(int row)
{
    const QString id = row >= 0 && row < int(m_themes.size()) ? m_themes[size_t(row)].id : QString();
    if (!isOpen() || id == m_presentation.themeId)
        return;
    m_presentation.themeId = id;
    touch();
}

// ---- saving ----------------------------------------------------------------

Result<QString> EditorController::assetFor(const DraftElement &element, const QString &libraryId)
{
    if (!element.mediaAssetId.isEmpty())
        return element.mediaAssetId;
    auto assets = Repository<MediaAsset>(*m_db).list();
    if (!assets.ok())
        return assets.error();
    for (const MediaAsset &asset : assets.value()) {
        if (asset.filePath == element.mediaPath)
            return asset.id;
    }
    auto created = Repository<MediaAsset>(*m_db).create({.libraryId = libraryId,
                                                         .filePath = element.mediaPath,
                                                         .hash = MediaLibrary::fingerprint(element.mediaPath),
                                                         .loop = element.style.value("loop").toBool()});
    if (!created.ok())
        return created.error();
    return created.value().id;
}

Status EditorController::saveSlides()
{
    // Groups (and their slides and elements) are written afresh, in order.
    auto groups = Repository<Group>(*m_db).listChildren(m_presentation.id);
    if (!groups.ok())
        return groups.error();
    for (const Group &g : groups.value()) {
        if (auto removed = Repository<Group>(*m_db).remove(g.id); !removed.ok())
            return removed;
    }
    QString groupId;
    QString groupLabel;
    int groupPosition = 0;
    int slidePosition = 0;
    for (const DraftSlide &slide : m_draft) {
        if (groupId.isEmpty() || slide.group != groupLabel) {
            auto group = Repository<Group>(*m_db).create(
                {.presentationId = m_presentation.id, .position = groupPosition++,
                 .label = slide.group.isEmpty() ? QStringLiteral("Slides") : slide.group});
            if (!group.ok())
                return group.error();
            groupId = group.value().id;
            groupLabel = slide.group;
            slidePosition = 0;
        }
        auto stored = Repository<Slide>(*m_db).create({.groupId = groupId, .position = slidePosition++, .notes = slide.notes,
                                                       .chords = slide.chords, .transition = slide.transition});
        if (!stored.ok())
            return stored.error();
        int z = 0;
        for (const DraftElement &e : slide.elements) {
            QString asset;
            if (!e.mediaPath.isEmpty() || !e.mediaAssetId.isEmpty()) {
                auto found = assetFor(e, m_presentation.libraryId);
                if (!found.ok())
                    return found.error();
                asset = found.value();
            }
            auto element = Repository<Element>(*m_db).create({.slideId = stored.value().id, .type = e.type, .frame = e.frame,
                                                               .style = e.style, .zOrder = z++, .content = e.content,
                                                               .mediaAssetId = asset});
            if (!element.ok())
                return element.error();
        }
    }
    return success();
}

QString EditorController::lyricsOf(const std::vector<std::pair<QString, QStringList>> &groups)
{
    QStringList parts;
    for (const auto &[label, slides] : groups)
        parts << QStringLiteral("[%1]\n%2").arg(label, slides.join(QStringLiteral("\n\n")));
    return parts.join(QStringLiteral("\n\n"));
}

QString EditorController::toSongText(const QString &lyrics)
{
    // Inside a group a blank line starts a slide ("---" to the importer).
    return songs::slideBreaksFromBlankLines(lyrics);
}

Status EditorController::saveArrangement(const QHash<QString, QString> &groupIds, const QStringList &order)
{
    auto arrangements = Repository<Arrangement>(*m_db).listChildren(m_presentation.id);
    for (const Arrangement &a : arrangements.ok() ? arrangements.value() : std::vector<Arrangement>{}) {
        if (auto removed = Repository<Arrangement>(*m_db).remove(a.id); !removed.ok())
            return removed;
    }
    QStringList ids;
    for (const QString &label : order) {
        if (groupIds.contains(label))
            ids << groupIds.value(label);
    }
    auto arrangement = Repository<Arrangement>(*m_db).create(
        {.presentationId = m_presentation.id, .name = tr("Default"), .isDefault = true, .groupIds = ids});
    return arrangement.ok() ? success() : Status(arrangement.error());
}

Status EditorController::saveSong()
{
    if (!m_lyricsChanged) {
        if (!m_arrangementChanged)
            return success();
        // Only the order changed: the groups stay as they are.
        QHash<QString, QString> groupIds;
        auto groups = Repository<Group>(*m_db).listChildren(m_presentation.id);
        for (const Group &g : groups.ok() ? groups.value() : std::vector<Group>{})
            groupIds.insert(g.label, g.id);
        return saveArrangement(groupIds, arrangement());
    }
    auto parsed = songs::parseTextSong(toSongText(m_lyrics), m_presentation.title);
    if (!parsed.ok())
        return parsed.error();
    const songs::ParsedSong &song = parsed.value();
    if (song.sections.empty())
        return invalid(tr("The lyrics have no lines"));
    auto groups = Repository<Group>(*m_db).listChildren(m_presentation.id);
    for (const Group &g : groups.ok() ? groups.value() : std::vector<Group>{}) {
        if (auto removed = Repository<Group>(*m_db).remove(g.id); !removed.ok())
            return removed;
    }
    const int linesPerSlide = m_songs ? m_songs->linesPerSlide() : 2;
    QHash<QString, QString> groupIds;
    int groupPosition = 0;
    for (const songs::ParsedSection &section : song.sections) {
        auto group = Repository<Group>(*m_db).create(
            {.presentationId = m_presentation.id, .position = groupPosition++, .label = section.label});
        if (!group.ok())
            return group.error();
        groupIds.insert(section.label, group.value().id);
        int slidePosition = 0;
        for (const auto &block : section.blocks) {
            for (const auto &lines : songs::splitIntoSlides(block, linesPerSlide)) {
                QStringList texts;
                for (const songs::LyricLine &line : lines)
                    texts << line.text;
                auto slide = Repository<Slide>(*m_db).create(
                    {.groupId = group.value().id, .position = slidePosition++, .chords = songs::slideChords(lines)});
                if (!slide.ok())
                    return slide.error();
                auto element = Repository<Element>(*m_db).create({.slideId = slide.value().id, .type = ElementType::Text,
                                                                  .frame = QRectF(96, 96, 1728, 888),
                                                                  .content = texts.join(u'\n')});
                if (!element.ok())
                    return element.error();
            }
        }
    }
    // The order set by hand, else the lyrics as written.
    return saveArrangement(groupIds, m_arrangementCustom ? arrangement() : song.order);
}

bool EditorController::save()
{
    if (!m_db || !isOpen())
        return false;
    const Status saved = m_db->inTransaction([&]() -> Status {
        if (auto updated = Repository<Presentation>(*m_db).update(m_presentation); !updated.ok())
            return updated;
        return isSong() ? saveSong() : saveSlides();
    });
    if (!saved.ok()) {
        m_message = tr("Not saved: %1").arg(saved.error().message);
        emit changed();
        return false;
    }
    if (isSong() && m_songs)
        (void)m_songs->reindex(m_presentation.id);
    const int slide = m_slide;
    load(m_presentation.id);
    m_slide = std::clamp(slide, 0, std::max(0, int(m_draft.size()) - 1));
    m_message = tr("Saved");
    m_op.refreshAfterEdit();
    refresh();
    return true;
}

void EditorController::cancel()
{
    if (!isOpen())
        return;
    const int slide = m_slide;
    load(m_presentation.id);
    m_slide = std::clamp(slide, 0, std::max(0, int(m_draft.size()) - 1));
    refresh();
}

} // namespace lumen::app
