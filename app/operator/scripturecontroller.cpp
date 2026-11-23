#include "scripturecontroller.h"

#include "themes.h"

#include "lumen/core/data/repository.h"

#include <QDir>
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

void ScriptureController::setVersesPerSlide(int count)
{
    count = qBound(1, count, 2);
    if (count == m_versesPerSlide)
        return;
    m_versesPerSlide = count;
    rebuild();
    emit changed();
}

void ScriptureController::lookup(const QString &text)
{
    m_query = text;
    refresh();
}

void ScriptureController::refresh()
{
    m_passage.reset();
    m_error.clear();
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

    const auto &verses = m_passage->verses;
    for (std::size_t i = 0; i < verses.size(); i += std::size_t(m_versesPerSlide)) {
        const std::vector<PassageVerse> chunk(verses.begin() + qsizetype(i),
                                              verses.begin() + qsizetype(qMin(i + std::size_t(m_versesPerSlide), verses.size())));
        const QString reference = slideReference(*m_passage, chunk);
        const QString footer = QStringLiteral("%1 (%2) · %3").arg(reference, t.abbreviation, t.copyright);
        const QStringList parts = core::show::splitToFit(slideText(chunk), theme.style, true);
        for (qsizetype p = 0; p < parts.size(); ++p) {
            const QString group = parts.size() > 1
                                      ? QStringLiteral("%1 (%2/%3)").arg(reference).arg(p + 1).arg(parts.size())
                                      : reference;
            view.slides.push_back({{}, group, scriptureColour, parts[p], footer, 0, {}, {}, {}});
        }
    }
    applyTheme(view, theme.style, theme.background);
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

bool ScriptureController::addToPlan()
{
    using namespace core::data;
    Database *db = m_op.database();
    if (!m_passage || !db)
        return false;
    const ItemView &view = m_item;
    const Translation &t = m_passage->translation;

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
             .title = QStringLiteral("%1 (%2)").arg(m_passage->label, t.abbreviation),
             .metadata = {{"subtitle", t.title}, {"copyright", t.copyright}, {"abbreviation", t.abbreviation},
                          {"translation", t.id}, {"reference", m_passage->label}}});
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
