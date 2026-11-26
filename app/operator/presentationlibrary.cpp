#include "presentationlibrary.h"

#include "medialibrary.h"

#include "lumen/core/data/repository.h"

#include <QDir>
#include <QFileInfo>

namespace lumen::app {

using namespace core;
using namespace core::data;

QString DeckImportReport::summary() const
{
    QStringList parts{QStringLiteral("%1 imported").arg(imported)};
    if (skipped > 0)
        parts << QStringLiteral("%1 skipped").arg(skipped);
    if (failed > 0)
        parts << QStringLiteral("%1 failed").arg(failed);
    return parts.join(QStringLiteral(", "));
}

PresentationLibrary::PresentationLibrary(Database &db, QString slideFolder)
    : m_db(db), m_slides(std::move(slideFolder))
{
    QDir().mkpath(m_slides);
}

QStringList PresentationLibrary::knownHashes() const
{
    QStringList hashes;
    auto presentations = Repository<Presentation>(m_db).list();
    if (!presentations.ok())
        return hashes;
    for (const Presentation &p : presentations.value()) {
        const QString hash = p.metadata.value(QStringLiteral("deckHash")).toString();
        if (p.kind == PresentationKind::Slides && !hash.isEmpty())
            hashes << hash;
    }
    return hashes;
}

std::vector<PresentationLibrary::Rendered> PresentationLibrary::render(const QStringList &paths, const QStringList &known,
                                                                       const QString &slideFolder,
                                                                       const DeckImporter::Options &options)
{
    std::vector<Rendered> rendered;
    QStringList seen = known;
    for (const QString &path : paths) {
        Rendered r;
        r.path = path;
        r.hash = QFileInfo(path).isFile() ? MediaLibrary::fingerprint(path) : QString();
        if (!r.hash.isEmpty() && seen.contains(r.hash)) {
            r.duplicate = true;
            rendered.push_back(std::move(r));
            continue;
        }
        // Each deck's pictures in a folder of their own, named after its
        // fingerprint (a failed or repeated import starts clean).
        const QString folder = QDir(slideFolder).filePath(r.hash.isEmpty() ? QStringLiteral("unreadable")
                                                                           : QString(r.hash).remove(0, 7).left(32));
        QDir(folder).removeRecursively();
        r.result = DeckImporter::import(path, folder, options);
        if (!r.result.ok())
            QDir(folder).removeRecursively();
        else
            seen << r.hash;
        rendered.push_back(std::move(r));
    }
    return rendered;
}

DeckImportReport PresentationLibrary::store(const std::vector<Rendered> &rendered)
{
    DeckImportReport report;
    auto library = Repository<Library>(m_db).list();
    QString libraryId = library.ok() && !library.value().empty() ? library.value().front().id : QString();
    if (libraryId.isEmpty()) {
        auto created = Repository<Library>(m_db).create({.name = "Main library"});
        if (created.ok())
            libraryId = created.value().id;
    }

    for (const Rendered &r : rendered) {
        const QString name = QFileInfo(r.path).fileName();
        if (r.duplicate) {
            ++report.skipped;
            report.messages << QStringLiteral("%1: already in the library").arg(name);
            continue;
        }
        if (!r.result.ok()) {
            ++report.failed;
            report.messages << QStringLiteral("%1: %2").arg(name, r.result.error.isEmpty() ? QStringLiteral("no slides") : r.result.error);
            continue;
        }
        const int count = int(r.result.slides.size());
        const Status stored = m_db.inTransaction([&]() -> Status {
            if (libraryId.isEmpty())
                return Error{Error::Code::Database, QStringLiteral("the library could not be created")};
            auto presentation = Repository<Presentation>(m_db).create(
                {.libraryId = libraryId,
                 .kind = PresentationKind::Slides,
                 .title = QFileInfo(r.path).completeBaseName(),
                 .metadata = {{"subtitle", count == 1 ? QStringLiteral("1 slide") : QStringLiteral("%1 slides").arg(count)},
                              {"deckHash", r.hash},
                              {"source", QFileInfo(r.path).absoluteFilePath()},
                              {"method", r.result.method},
                              {"slides", count},
                              {"thumbnail", r.result.thumbnails.value(0)}}});
            if (!presentation.ok())
                return presentation.error();
            auto group = Repository<Group>(m_db).create({.presentationId = presentation.value().id, .label = QStringLiteral("Slides")});
            if (!group.ok())
                return group.error();
            for (int i = 0; i < count; ++i) {
                auto asset = Repository<MediaAsset>(m_db).create({.libraryId = libraryId,
                                                                  .filePath = r.result.slides[i],
                                                                  .hash = QStringLiteral("%1#%2").arg(r.hash).arg(i + 1),
                                                                  .thumbnailPath = r.result.thumbnails.value(i)});
                if (!asset.ok())
                    return asset.error();
                auto slide = Repository<Slide>(m_db).create({.groupId = group.value().id, .position = i});
                if (!slide.ok())
                    return slide.error();
                auto element = Repository<Element>(m_db).create({.slideId = slide.value().id,
                                                                 .type = ElementType::Image,
                                                                 .frame = QRectF(0, 0, 1920, 1080),
                                                                 .mediaAssetId = asset.value().id});
                if (!element.ok())
                    return element.error();
            }
            return success();
        });
        if (!stored.ok()) {
            ++report.failed;
            report.messages << QStringLiteral("%1: %2").arg(name, stored.error().message);
            continue;
        }
        ++report.imported;
        QString line = QStringLiteral("%1: %2 %3").arg(name).arg(count).arg(count == 1 ? QStringLiteral("slide") : QStringLiteral("slides"));
        if (r.result.hiddenSkipped > 0)
            line += QStringLiteral(", %1 hidden left out").arg(r.result.hiddenSkipped);
        if (!r.result.note.isEmpty())
            line += QStringLiteral(" (%1)").arg(r.result.note);
        report.messages << line;
    }
    return report;
}

Result<std::vector<DeckRow>> PresentationLibrary::list() const
{
    auto presentations = Repository<Presentation>(m_db).list();
    if (!presentations.ok())
        return presentations.error();
    std::vector<DeckRow> rows;
    for (const Presentation &p : presentations.value()) {
        if (p.kind != PresentationKind::Slides || !p.metadata.contains(QStringLiteral("deckHash")))
            continue;
        rows.push_back({p.id, p.title, p.metadata.value(QStringLiteral("slides")).toInt(),
                        p.metadata.value(QStringLiteral("thumbnail")).toString(),
                        p.metadata.value(QStringLiteral("method")).toString()});
    }
    return rows;
}

} // namespace lumen::app
