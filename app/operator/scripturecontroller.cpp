#include "scripturecontroller.h"

#include "themes.h"

#include "lumen/core/data/repository.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace lumen::app {

using namespace core;
using namespace core::scripture;

namespace {

const QColor scriptureColour(0x3F, 0xB7, 0xA6);

// "John 3:16" or "John 3:16–17" for the verses on one slide.
QString slideReference(const Passage &passage, const std::vector<PassageVerse> &verses)
{
    Reference ref = passage.reference;
    ref.chapter = verses.front().chapter;
    ref.verse = verses.front().verse;
    ref.endChapter = verses.back().chapter;
    ref.endVerse = verses.back().last;
    return formatReference(ref, passage.bookName);
}

QString slideText(const std::vector<PassageVerse> &verses)
{
    if (verses.size() == 1)
        return verses.front().text;
    QStringList parts;
    for (const PassageVerse &verse : verses)
        parts << QStringLiteral("%1 %2").arg(verse.verse).arg(verse.text);
    return parts.join(u' ');
}

} // namespace

ScriptureController::ScriptureController(OperatorController &op, QObject *parent)
    : QObject(parent), m_op(op)
{}

void ScriptureController::setStores(BibleStore *bundled, BibleStore *user)
{
    m_bundled = bundled;
    m_user = user;
    reload();
}

void ScriptureController::reload()
{
    m_sources.clear();
    m_names = BookNames();
    for (BibleStore *store : {m_bundled, m_user}) {
        if (!store)
            continue;
        auto list = store->translations();
        if (!list.ok()) {
            qWarning("Bibles not read: %s", qPrintable(list.error().message));
            continue;
        }
        for (const Translation &t : list.value())
            m_sources.push_back({store, t});
        (void)store->addBookNames(m_names);
    }
    m_translation = qBound(0, m_translation, qMax(0, int(m_sources.size()) - 1));
    if (m_second >= int(m_sources.size()))
        m_second = -1;
    refresh();
}

QVariantList ScriptureController::translations() const
{
    QVariantList list;
    for (const Source &source : m_sources) {
        list << QVariantMap{{"id", source.translation.id},
                            {"abbreviation", source.translation.abbreviation},
                            {"title", source.translation.title},
                            {"language", source.translation.language},
                            {"licence", source.translation.licence},
                            {"copyright", source.translation.copyright}};
    }
    return list;
}

void ScriptureController::setTranslation(int index)
{
    if (index < 0 || index >= int(m_sources.size()) || index == m_translation)
        return;
    m_translation = index;
    refresh();
}

void ScriptureController::setReferencePosition(const QString &position)
{
    static const QStringList known{QStringLiteral("below"), QStringLiteral("above"), QStringLiteral("lowerThird")};
    if (!known.contains(position) || position == m_referencePosition)
        return;
    m_referencePosition = position;
    rebuild();
    emit changed();
}

void ScriptureController::setSecondTranslation(int index)
{
    if (index < -1 || index >= int(m_sources.size()))
        index = -1;
    if (index == m_second)
        return;
    m_second = index;
    refresh();
}

void ScriptureController::setShowCopyright(bool show)
{
    if (show == m_showCopyright)
        return;
    m_showCopyright = show;
    rebuild();
    emit changed();
}

QString ScriptureController::shortCredit(const Translation &t)
{
    static const QRegularExpression holder(QStringLiteral(R"(©\s*(?:\d{4}[,\s]*)*([^.,]+?)(?:,?\s*Inc)?\.)"));
    const auto m = holder.match(t.copyright);
    if (!m.hasMatch() || t.licence.isEmpty())
        return t.copyright;
    return QStringLiteral("%1 © %2, %3").arg(t.abbreviation, m.captured(1).trimmed(), t.licence);
}

QString ScriptureController::credit(bool firstSlide) const
{
    if (!m_showCopyright && !firstSlide)
        return {};
    QStringList lines;
    for (const auto *passage : {m_passage ? &*m_passage : nullptr, m_secondPassage ? &*m_secondPassage : nullptr}) {
        if (!passage)
            continue;
        const Translation &t = passage->translation;
        // Public domain needs no credit; anything else (CC BY-SA) does.
        const bool required = !t.licence.startsWith(QLatin1String("Public domain"), Qt::CaseInsensitive);
        if (m_showCopyright && !t.copyright.isEmpty())
            lines << t.copyright;
        else if (required && !t.copyright.isEmpty())
            lines << shortCredit(t);
    }
    return lines.join(QStringLiteral(" · "));
}

QString ScriptureController::previewSecondText() const
{
    if (m_item.slides.empty() || m_item.slides.front().elements.size() < 2)
        return {};
    return m_item.slides.front().elements.at(1).toObject().value(QStringLiteral("text")).toString();
}

void ScriptureController::setVersesPerSlide(int count)
{
    count = qBound(0, count, 2);
    if (count == m_versesPerSlide)
        return;
    m_versesPerSlide = count;
    rebuild();
    emit changed();
}

QString ScriptureController::bookFor(const QString &typed) const
{
    const QString text = typed.trimmed();
    if (text.isEmpty() || m_sources.empty())
        return {};
    const Source &source = m_sources[std::size_t(m_translation)];
    auto books = source.store->books(source.translation.id);
    if (!books.ok())
        return {};
    // The suggestion for the book alone is "<Name> 1:1".
    const QString suggestion = completeReference(text, m_names, books.value()).suggestion;
    return suggestion.endsWith(QLatin1String(" 1:1")) ? suggestion.chopped(4) : QString();
}

void ScriptureController::lookup(const QString &text)
{
    m_query = text;
    refresh();
}

void ScriptureController::refresh()
{
    m_passage.reset();
    m_secondPassage.reset();
    m_error.clear();
    m_completion = {};
    if (!m_sources.empty() && !m_query.trimmed().isEmpty()) {
        const Source &source = m_sources[std::size_t(m_translation)];
        if (auto books = source.store->books(source.translation.id); books.ok())
            m_completion = completeReference(m_query, m_names, books.value());
    }
    if (m_sources.empty()) {
        m_error = tr("No Bibles are installed.");
    } else if (!m_query.trimmed().isEmpty()) {
        auto reference = parseReference(m_query, m_names);
        if (!reference.ok()) {
            m_error = reference.error().message;
        } else {
            const Source &source = m_sources[std::size_t(m_translation)];
            auto passage = source.store->passage(source.translation.id, reference.value());
            if (passage.ok())
                m_passage = std::move(passage).value();
            else
                m_error = passage.error().message;
            // The same verses in the second translation (it may lack some).
            if (m_passage && m_second >= 0 && m_second != m_translation) {
                const Source &other = m_sources[std::size_t(m_second)];
                if (auto second = other.store->passage(other.translation.id, reference.value()); second.ok())
                    m_secondPassage = std::move(second).value();
            }
        }
    }
    rebuild();
    emit changed();
}

QVariantList ScriptureController::verses() const
{
    QVariantList list;
    if (!m_passage)
        return list;
    const bool manyChapters = m_passage->reference.endChapter != m_passage->reference.chapter;
    for (const PassageVerse &verse : m_passage->verses) {
        const QString number = manyChapters ? QStringLiteral("%1:%2").arg(verse.chapter).arg(verse.verse)
                                            : QString::number(verse.verse);
        list << QVariantMap{{"number", verse.last > verse.verse ? number + u'-' + QString::number(verse.last) : number},
                            {"text", verse.text}};
    }
    return list;
}

QString ScriptureController::copyright() const
{
    if (m_sources.empty())
        return {};
    return m_sources[std::size_t(m_translation)].translation.copyright;
}

void ScriptureController::rebuild()
{
    m_item = {};
    m_firstSlideVerses = 0;
    if (!m_passage)
        return;
    const Translation &t = m_passage->translation;
    ItemView &view = m_item;
    view.kind = QStringLiteral("BIBLE");
    view.title = m_passage->label;
    view.subtitle = t.title;
    view.detail = t.copyright;
    view.presentationId = QStringLiteral("scripture:%1:%2").arg(t.id, m_passage->label);

    // The scripture theme: the library's default, else the built-in one.
    ThemeView theme = builtInTheme(core::data::ThemeTarget::Scripture);
    if (m_op.database()) {
        if (const auto stored = defaultTheme(*m_op.database(), core::data::ThemeTarget::Scripture))
            theme = *stored;
    }

    const core::show::TextStyle style = scriptureStyle(theme.style, m_referencePosition);
    // Side by side: each column is a little under half the width.
    core::show::TextStyle column = style;
    column.safeArea.setWidth(style.safeArea.width() / 2 - 0.02);
    const bool sideBySide = m_secondPassage.has_value();
    const Translation *second = sideBySide ? &m_secondPassage->translation : nullptr;

    // The second translation's verses for the same range.
    const auto secondText = [&](const std::vector<PassageVerse> &chunk) {
        std::vector<PassageVerse> matching;
        const auto key = [](const PassageVerse &v) { return std::pair(v.chapter, v.verse); };
        for (const PassageVerse &v : m_secondPassage->verses) {
            if (key(v) >= key(chunk.front()) && key(v) <= std::pair(chunk.back().chapter, chunk.back().last))
                matching.push_back(v);
        }
        return matching.empty() ? QString() : slideText(matching);
    };
    const auto fits = [&](const std::vector<PassageVerse> &chunk) {
        if (!sideBySide)
            return core::show::fitText(slideText(chunk), style, true).fits;
        return core::show::fitText(slideText(chunk), column, true).fits
               && core::show::fitText(secondText(chunk), column, true).fits;
    };

    // Verses per slide: 1 or 2, or 0: as many as fit.
    const auto &verses = m_passage->verses;
    std::vector<std::vector<PassageVerse>> chunks;
    for (std::size_t i = 0; i < verses.size();) {
        std::size_t end = i + 1;
        if (m_versesPerSlide > 0) {
            end = qMin(i + std::size_t(m_versesPerSlide), verses.size());
        } else {
            while (end < verses.size()
                   && fits(std::vector<PassageVerse>(verses.begin() + qsizetype(i), verses.begin() + qsizetype(end + 1))))
                ++end;
        }
        chunks.emplace_back(verses.begin() + qsizetype(i), verses.begin() + qsizetype(end));
        i = end;
    }

    m_firstSlideVerses = chunks.empty() ? 0 : int(chunks.front().size());
    for (const auto &chunk : chunks) {
        const bool firstSlide = view.slides.empty();
        const QString reference = slideReference(*m_passage, chunk);
        if (sideBySide) {
            // Each column names its own passage, in its own language.
            std::vector<PassageVerse> matching;
            for (const PassageVerse &v : m_secondPassage->verses) {
                if (std::pair(v.chapter, v.verse) >= std::pair(chunk.front().chapter, chunk.front().verse)
                    && std::pair(v.chapter, v.verse) <= std::pair(chunk.back().chapter, chunk.back().last))
                    matching.push_back(v);
            }
            const QString leftReference = QStringLiteral("%1 (%2)").arg(reference, t.abbreviation);
            const QString rightReference = matching.empty()
                                               ? second->abbreviation
                                               : QStringLiteral("%1 (%2)").arg(slideReference(*m_secondPassage, matching), second->abbreviation);
            const QString footer = credit(firstSlide);
            SlideView slide{{}, reference, scriptureColour, slideText(chunk), footer, 0, {}, {}, {}, false, {}, {}};
            slide.elements = sideBySideElements(style, slideText(chunk), leftReference, secondText(chunk), rightReference, footer);
            view.slides.push_back(std::move(slide));
            continue;
        }
        const QString lineCredit = credit(firstSlide);
        const QString footer = lineCredit.isEmpty() ? QStringLiteral("%1 (%2)").arg(reference, t.abbreviation)
                                                    : QStringLiteral("%1 (%2) · %3").arg(reference, t.abbreviation, lineCredit);
        const QStringList parts = core::show::splitToFit(slideText(chunk), style, true);
        for (qsizetype p = 0; p < parts.size(); ++p) {
            const QString group = parts.size() > 1
                                      ? QStringLiteral("%1 (%2/%3)").arg(reference).arg(p + 1).arg(parts.size())
                                      : reference;
            // Hidden: the credit on the passage's very first slide only.
            const QString partFooter = p == 0 || m_showCopyright ? footer : QStringLiteral("%1 (%2)").arg(reference, t.abbreviation);
            view.slides.push_back({{}, group, scriptureColour, parts[p], partFooter, 0, {}, {}, {}, false, {}, {}});
        }
    }
    if (sideBySide) {
        view.title = QStringLiteral("%1 (%2 + %3)").arg(m_passage->label, t.abbreviation, second->abbreviation);
        view.detail = t.copyright + QStringLiteral(" · ") + second->copyright;
    }
    applyTheme(view, style, theme.background);
}

QString ScriptureController::previewText() const
{
    return m_item.slides.empty() ? QString() : m_item.slides.front().text;
}

QString ScriptureController::previewReference() const
{
    return m_item.slides.empty() ? QString() : m_item.slides.front().footer.section(QStringLiteral(" · "), 0, 0);
}

bool ScriptureController::showNow()
{
    if (!m_passage || !m_op.showItem(m_item))
        return false;
    return m_op.goLive(0);
}

bool ScriptureController::preview()
{
    return m_passage && m_op.showItem(m_item);
}

bool ScriptureController::addToPlan()
{
    using namespace core::data;
    Database *db = m_op.database();
    if (!m_passage || !db)
        return false;
    const ItemView &view = m_item;
    const Translation &t = m_passage->translation;

    QJsonObject metadata{{"subtitle", t.title},           {"copyright", credit()},
                         {"creditFirstSlideOnly", !m_showCopyright},
                         {"abbreviation", t.abbreviation}, {"translation", t.id},
                         {"reference", m_passage->label},  {"referencePosition", m_referencePosition}};
    if (m_secondPassage) {
        const Translation &second = m_secondPassage->translation;
        metadata.insert("secondTranslation", second.id);
        metadata.insert("secondAbbreviation", second.abbreviation);
        metadata.insert("secondCopyright", second.copyright);
    }

    QString presentationId;
    const Status stored = db->inTransaction([&]() -> Status {
        auto libraries = Repository<Library>(*db).list();
        if (!libraries.ok())
            return libraries.error();
        QString libraryId;
        if (!libraries.value().empty()) {
            libraryId = libraries.value().front().id;
        } else {
            auto created = Repository<Library>(*db).create({.name = "Main library"});
            if (!created.ok())
                return created.error();
            libraryId = created.value().id;
        }
        auto presentation = Repository<Presentation>(*db).create(
            {.libraryId = libraryId,
             .kind = PresentationKind::Scripture,
             .title = m_secondPassage ? view.title : QStringLiteral("%1 (%2)").arg(m_passage->label, t.abbreviation),
             .metadata = metadata});
        if (!presentation.ok())
            return presentation.error();
        presentationId = presentation.value().id;
        int position = 0;
        for (const SlideView &slide : view.slides) {
            auto group = Repository<Group>(*db).create({.presentationId = presentationId,
                                                        .position = position++,
                                                        .label = slide.group,
                                                        .colour = scriptureColour.name()});
            if (!group.ok())
                return group.error();
            auto stored = Repository<Slide>(*db).create({.groupId = group.value().id});
            if (!stored.ok())
                return stored.error();
            if (!slide.elements.isEmpty()) {
                // Side by side: the columns and footer as placed.
                int z = 0;
                for (const QJsonValue &value : slide.elements) {
                    const QJsonObject e = value.toObject();
                    const QJsonArray f = e.value("frame").toArray();
                    auto element = Repository<Element>(*db).create(
                        {.slideId = stored.value().id,
                         .type = ElementType::Text,
                         .frame = QRectF(f.at(0).toDouble(), f.at(1).toDouble(), f.at(2).toDouble(), f.at(3).toDouble()),
                         .style = e.value("style").toObject(),
                         .zOrder = z++,
                         .content = e.value("text").toString()});
                    if (!element.ok())
                        return element.error();
                }
                continue;
            }
            auto element = Repository<Element>(*db).create({.slideId = stored.value().id,
                                                            .type = ElementType::Text,
                                                            .frame = QRectF(96, 96, 1728, 888),
                                                            .content = slide.text});
            if (!element.ok())
                return element.error();
        }
        return success();
    });
    if (!stored.ok()) {
        qWarning("Scripture not added to the plan: %s", qPrintable(stored.error().message));
        return false;
    }
    return m_op.addPresentationToPlan(presentationId);
}

QString ScriptureController::addBible(const QUrl &location)
{
    const QString path = location.isLocalFile() ? location.toLocalFile() : location.toString();
    if (!m_user) {
        m_addSummary = tr("Bibles cannot be added: the user Bible file is not open.");
        emit changed();
        return m_addSummary;
    }
    auto books = readBibleFiles(path);
    if (!books.ok()) {
        m_addSummary = books.error().message;
        emit changed();
        return m_addSummary;
    }

    const QFileInfo info(path);
    const QString name = info.isDir() ? info.fileName() : info.completeBaseName();
    QString id = name.toLower();
    id.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]")), QStringLiteral("_"));
    // A copyright page beside the files, if the package has one.
    QString notice;
    QFile page(QDir(info.isDir() ? path : info.path()).filePath(QStringLiteral("copr.htm")));
    if (page.open(QIODevice::ReadOnly)) {
        notice = QString::fromUtf8(page.readAll());
        notice.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QStringLiteral(" "));
        notice = notice.simplified();
    }
    const Translation translation{QStringLiteral("user-") + id,
                                  name.left(12),
                                  name,
                                  QString(),
                                  tr("Added from a file"),
                                  notice.isEmpty() ? tr("%1. Added from a file: check its licence before public use.").arg(name)
                                                   : notice.left(160),
                                  path,
                                  notice};
    if (auto added = m_user->add(translation, books.value()); !added.ok()) {
        m_addSummary = added.error().message;
        emit changed();
        return m_addSummary;
    }
    m_addSummary = tr("Added %1 (%2 books)").arg(name).arg(books.value().size());
    reload();
    return m_addSummary;
}

} // namespace lumen::app
