#include "lumenfile.h"

#include "lumen/core/buildinfo.h"
#include "lumen/core/data/repository.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <private/qzipreader_p.h>
#include <private/qzipwriter_p.h>

#include <map>

namespace lumen::app::lumenfile {

using namespace core;
using namespace core::data;

namespace {

const std::map<PresentationKind, QString> kindNames{{PresentationKind::Song, "song"},
                                                    {PresentationKind::Scripture, "scripture"},
                                                    {PresentationKind::Slides, "slides"},
                                                    {PresentationKind::Media, "media"}};
const std::map<ElementType, QString> typeNames{{ElementType::Text, "text"},   {ElementType::Image, "image"},
                                               {ElementType::Video, "video"}, {ElementType::Shape, "shape"},
                                               {ElementType::Timer, "timer"}, {ElementType::Clock, "clock"}};

template <typename Enum>
std::optional<Enum> fromName(const std::map<Enum, QString> &names, const QString &name)
{
    for (const auto &[value, text] : names) {
        if (text == name)
            return value;
    }
    return std::nullopt;
}

Error invalid(const QString &message)
{
    return Error{Error::Code::Invalid, message};
}

// What a presentation shows, to tell whether one being imported is already
// in the library: its kind, title, group labels and element contents.
QString fingerprint(const QJsonObject &presentation)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(presentation.value("kind").toString().toUtf8());
    hash.addData(presentation.value("title").toString().toUtf8());
    for (const QJsonValue &group : presentation.value("groups").toArray()) {
        hash.addData("|g:" + group.toObject().value("label").toString().toUtf8());
        for (const QJsonValue &slide : group.toObject().value("slides").toArray()) {
            hash.addData("|s");
            for (const QJsonValue &element : slide.toObject().value("elements").toArray()) {
                const QJsonObject e = element.toObject();
                hash.addData("|e:" + e.value("type").toString().toUtf8() + ":" + e.value("content").toString().toUtf8()
                             + ":" + e.value("mediaHash").toString().toUtf8());
            }
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

// A presentation as JSON (the shape of lumen.json's "presentations").
Result<QJsonObject> presentationJson(Database &db, const QString &id, QJsonArray &media, QHash<QString, QString> &mediaIds)
{
    auto p = Repository<Presentation>(db).get(id);
    if (!p.ok())
        return p.error();
    QJsonObject out{{"id", p.value().id},
                    {"kind", kindNames.at(p.value().kind)},
                    {"title", p.value().title},
                    {"metadata", p.value().metadata}};
    if (!p.value().themeId.isEmpty()) {
        if (auto theme = Repository<Theme>(db).get(p.value().themeId); theme.ok())
            out.insert("theme", theme.value().name);
    }
    auto groups = Repository<Group>(db).listChildren(id);
    if (!groups.ok())
        return groups.error();
    QJsonArray groupsJson;
    for (const Group &group : groups.value()) {
        auto slides = Repository<Slide>(db).listChildren(group.id);
        if (!slides.ok())
            return slides.error();
        QJsonArray slidesJson;
        for (const Slide &slide : slides.value()) {
            auto elements = Repository<Element>(db).listChildren(slide.id);
            if (!elements.ok())
                return elements.error();
            QJsonArray elementsJson;
            for (const Element &e : elements.value()) {
                QJsonObject element{{"type", typeNames.at(e.type)},
                                    {"frame", QJsonArray{e.frame.x(), e.frame.y(), e.frame.width(), e.frame.height()}},
                                    {"style", e.style},
                                    {"z", e.zOrder},
                                    {"content", e.content}};
                if (!e.mediaAssetId.isEmpty()) {
                    auto asset = Repository<MediaAsset>(db).get(e.mediaAssetId);
                    if (!asset.ok())
                        return asset.error();
                    if (!mediaIds.contains(asset.value().id)) {
                        const QString name = QStringLiteral("media/%1-%2").arg(media.size() + 1).arg(QFileInfo(asset.value().filePath).fileName());
                        QJsonObject m{{"id", asset.value().id},
                                      {"file", name},
                                      {"hash", asset.value().hash},
                                      {"durationMs", asset.value().durationMs},
                                      {"loop", asset.value().loop},
                                      {"trimInMs", asset.value().trimInMs},
                                      {"trimOutMs", asset.value().trimOutMs},
                                      {"source", asset.value().filePath}};
                        if (!asset.value().thumbnailPath.isEmpty() && QFileInfo::exists(asset.value().thumbnailPath))
                            m.insert("thumbnail", QStringLiteral("media/%1-thumb-%2").arg(media.size() + 1).arg(QFileInfo(asset.value().thumbnailPath).fileName()));
                        m.insert("thumbnailSource", asset.value().thumbnailPath);
                        media.append(m);
                        mediaIds.insert(asset.value().id, asset.value().hash);
                    }
                    element.insert("media", asset.value().id);
                    element.insert("mediaHash", mediaIds.value(asset.value().id));
                }
                elementsJson.append(element);
            }
            slidesJson.append(QJsonObject{{"notes", slide.notes},
                                          {"chords", slide.chords},
                                          {"transition", slide.transition},
                                          {"elements", elementsJson}});
        }
        groupsJson.append(QJsonObject{{"id", group.id},
                                      {"label", group.label},
                                      {"colour", group.colour},
                                      {"hotkey", group.hotkey},
                                      {"slides", slidesJson}});
    }
    out.insert("groups", groupsJson);
    auto arrangements = Repository<Arrangement>(db).listChildren(id);
    if (!arrangements.ok())
        return arrangements.error();
    QJsonArray arrangementsJson;
    for (const Arrangement &a : arrangements.value()) {
        arrangementsJson.append(QJsonObject{{"id", a.id},
                                            {"name", a.name},
                                            {"default", a.isDefault},
                                            {"groups", QJsonArray::fromStringList(a.groupIds)}});
    }
    out.insert("arrangements", arrangementsJson);
    out.insert("fingerprint", fingerprint(out));
    return out;
}

QString libraryId(Database &db)
{
    auto libraries = Repository<Library>(db).list();
    if (libraries.ok() && !libraries.value().empty())
        return libraries.value().front().id;
    auto created = Repository<Library>(db).create({.name = "Main library"});
    return created.ok() ? created.value().id : QString();
}

} // namespace

QString ImportReport::summary() const
{
    if (planId.isEmpty())
        return QStringLiteral("Imported %1 new and %2 already in the library; %3 media files copied, %4 already here")
            .arg(presentationsAdded)
            .arg(presentationsReused)
            .arg(mediaAdded)
            .arg(mediaReused);
    return QStringLiteral("Imported \"%1\": %2 new and %3 already in the library; %4 media files copied, %5 already here")
        .arg(planName)
        .arg(presentationsAdded)
        .arg(presentationsReused)
        .arg(mediaAdded)
        .arg(mediaReused);
}

namespace {

// Writes lumen.json and the media files into a .lumen archive.
Status writeArchive(const QJsonObject &root, const QJsonArray &media, const QString &file)
{
    // Written to a temporary name first, so a failed export never leaves a
    // half-written file under the real name.
    const QString partial = file + QStringLiteral(".part");
    QFile::remove(partial);
    {
        QZipWriter zip(partial);
        if (zip.status() != QZipWriter::NoError)
            return Error{Error::Code::Database, QStringLiteral("%1 could not be written").arg(file)};
        zip.setCompressionPolicy(QZipWriter::AutoCompress);
        zip.addFile(QStringLiteral("lumen.json"), QJsonDocument(root).toJson(QJsonDocument::Indented));
        for (const QJsonValue &value : media) {
            const QJsonObject m = value.toObject();
            QFile source(m.value("source").toString());
            if (!source.open(QIODevice::ReadOnly)) {
                zip.close();
                QFile::remove(partial);
                return Error{Error::Code::NotFound, QStringLiteral("Media file missing: %1").arg(source.fileName())};
            }
            // Videos and pictures are already compressed.
            zip.setCompressionPolicy(QZipWriter::NeverCompress);
            zip.addFile(m.value("file").toString(), &source);
            if (m.contains("thumbnail")) {
                QFile thumb(m.value("thumbnailSource").toString());
                if (thumb.open(QIODevice::ReadOnly))
                    zip.addFile(m.value("thumbnail").toString(), &thumb);
            }
        }
        zip.close();
        if (zip.status() != QZipWriter::NoError) {
            QFile::remove(partial);
            return Error{Error::Code::Database, QStringLiteral("%1 could not be written").arg(file)};
        }
    }
    QFile::remove(file);
    if (!QFile::rename(partial, file))
        return Error{Error::Code::Database, QStringLiteral("%1 could not be written").arg(file)};
    return success();
}

} // namespace

Status exportPlan(Database &db, const QString &planId, const QString &file)
{
    auto plan = Repository<Plan>(db).get(planId);
    if (!plan.ok())
        return plan.error();
    auto items = Repository<PlanItem>(db).listChildren(planId);
    if (!items.ok())
        return items.error();

    QJsonArray media;
    QHash<QString, QString> mediaIds;
    QJsonArray presentations;
    QStringList exported;
    QJsonArray itemsJson;
    for (const PlanItem &item : items.value()) {
        QJsonObject row{{"header", item.headerText}, {"notes", item.notes}};
        if (!item.presentationId.isEmpty()) {
            row.insert("presentation", item.presentationId);
            if (!item.arrangementId.isEmpty())
                row.insert("arrangement", item.arrangementId);
            if (!exported.contains(item.presentationId)) {
                auto json = presentationJson(db, item.presentationId, media, mediaIds);
                if (!json.ok())
                    return json.error();
                presentations.append(json.value());
                exported << item.presentationId;
            }
        }
        itemsJson.append(row);
    }
    const QJsonObject root{
        {"format", "lumen"},
        {"version", formatVersion},
        {"app", QStringLiteral("Lumen Presenter %1").arg(core::version())},
        {"plan", QJsonObject{{"name", plan.value().name},
                             {"date", plan.value().date.isValid() ? plan.value().date.toString(Qt::ISODate) : QString()},
                             {"items", itemsJson}}},
        {"presentations", presentations},
        {"media", media}};

    return writeArchive(root, media, file);
}

Status exportPresentations(Database &db, const QStringList &presentationIds, const QString &file)
{
    QJsonArray media;
    QHash<QString, QString> mediaIds;
    QJsonArray presentations;
    for (const QString &id : presentationIds) {
        auto json = presentationJson(db, id, media, mediaIds);
        if (!json.ok())
            return json.error();
        presentations.append(json.value());
    }
    const QJsonObject root{{"format", "lumen"},
                           {"version", formatVersion},
                           {"app", QStringLiteral("Lumen Presenter %1").arg(core::version())},
                           {"presentations", presentations},
                           {"media", media}};
    return writeArchive(root, media, file);
}

Result<ImportReport> importFile(Database &db, const QString &file, const QString &mediaFolder)
{
    QZipReader zip(file);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError)
        return invalid(QStringLiteral("%1 is not a .lumen file").arg(QFileInfo(file).fileName()));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(zip.fileData(QStringLiteral("lumen.json")), &parseError);
    const QJsonObject root = doc.object();
    if (doc.isNull() || root.value("format").toString() != QLatin1String("lumen"))
        return invalid(QStringLiteral("%1 is not a .lumen file").arg(QFileInfo(file).fileName()));
    if (root.value("version").toInt() > formatVersion)
        return invalid(QStringLiteral("%1 was made by a newer Lumen; update Lumen to open it").arg(QFileInfo(file).fileName()));

    QSet<QString> entries;
    for (const QZipReader::FileInfo &entry : zip.fileInfoList())
        entries.insert(entry.filePath);

    ImportReport report;
    const QString library = libraryId(db);
    if (library.isEmpty())
        return Error{Error::Code::Database, QStringLiteral("The library could not be created")};

    // Files written so far, removed again if the import fails.
    QStringList written;
    const Status done = db.inTransaction([&]() -> Status {
        // Media: reuse by content hash, else copy out of the archive.
        QHash<QString, QString> assetIds; // id in the file -> id here
        auto existingAssets = Repository<MediaAsset>(db).list();
        if (!existingAssets.ok())
            return existingAssets.error();
        // A folder per import, so files from different plans never collide.
        QString folder = QDir(mediaFolder).filePath(QFileInfo(file).completeBaseName());
        if (QFileInfo::exists(folder))
            folder += QDateTime::currentDateTime().toString(QStringLiteral("-yyyyMMdd-HHmmss"));
        for (const QJsonValue &value : root.value("media").toArray()) {
            const QJsonObject m = value.toObject();
            const QString hash = m.value("hash").toString();
            const auto same = std::find_if(existingAssets.value().begin(), existingAssets.value().end(), [&](const MediaAsset &a) {
                return !hash.isEmpty() && a.hash == hash && QFileInfo::exists(a.filePath);
            });
            if (same != existingAssets.value().end()) {
                assetIds.insert(m.value("id").toString(), same->id);
                ++report.mediaReused;
                continue;
            }
            const QString name = m.value("file").toString();
            if (!entries.contains(name))
                return invalid(QStringLiteral("The file is incomplete: %1 is missing").arg(name));
            QDir().mkpath(folder);
            const QString target = QDir(folder).filePath(QFileInfo(name).fileName());
            QFile out(target);
            if (!out.open(QIODevice::WriteOnly))
                return Error{Error::Code::Database, QStringLiteral("Could not write %1").arg(target)};
            out.write(zip.fileData(name));
            out.close();
            written << target;
            QString thumbnail;
            if (const QString thumbName = m.value("thumbnail").toString(); !thumbName.isEmpty() && entries.contains(thumbName)) {
                thumbnail = QDir(folder).filePath(QFileInfo(thumbName).fileName());
                QFile thumb(thumbnail);
                if (thumb.open(QIODevice::WriteOnly))
                    thumb.write(zip.fileData(thumbName));
                written << thumbnail;
            }
            auto asset = Repository<MediaAsset>(db).create({.libraryId = library,
                                                            .filePath = target,
                                                            .hash = hash,
                                                            .durationMs = m.value("durationMs").toInteger(),
                                                            .thumbnailPath = thumbnail,
                                                            .loop = m.value("loop").toBool(),
                                                            .trimInMs = m.value("trimInMs").toInteger(),
                                                            .trimOutMs = m.value("trimOutMs").toInteger()});
            if (!asset.ok())
                return asset.error();
            assetIds.insert(m.value("id").toString(), asset.value().id);
            ++report.mediaAdded;
        }

        // Presentations: reuse one with the same fingerprint, else add.
        QHash<QString, QString> presentationIds;  // id in the file -> id here
        QHash<QString, QString> arrangementIds;   // id in the file -> id here
        auto existing = Repository<Presentation>(db).list();
        if (!existing.ok())
            return existing.error();
        auto themes = Repository<Theme>(db).list();
        for (const QJsonValue &value : root.value("presentations").toArray()) {
            const QJsonObject p = value.toObject();
            const QString fileId = p.value("id").toString();
            const auto kind = fromName(kindNames, p.value("kind").toString());
            if (!kind)
                return invalid(QStringLiteral("Unknown presentation kind \"%1\"").arg(p.value("kind").toString()));
            const QString print = fingerprint(p);

            QString reuse;
            for (const Presentation &candidate : existing.value()) {
                if (candidate.kind != *kind || candidate.title != p.value("title").toString())
                    continue;
                QJsonArray ignored;
                QHash<QString, QString> ignoredIds;
                auto json = presentationJson(db, candidate.id, ignored, ignoredIds);
                if (json.ok() && json.value().value("fingerprint").toString() == print) {
                    reuse = candidate.id;
                    // Arrangements are matched by name.
                    auto ours = Repository<Arrangement>(db).listChildren(candidate.id);
                    for (const QJsonValue &a : p.value("arrangements").toArray()) {
                        for (const Arrangement &mine : ours.ok() ? ours.value() : std::vector<Arrangement>{}) {
                            if (mine.name == a.toObject().value("name").toString())
                                arrangementIds.insert(a.toObject().value("id").toString(), mine.id);
                        }
                    }
                    break;
                }
            }
            if (!reuse.isEmpty()) {
                presentationIds.insert(fileId, reuse);
                ++report.presentationsReused;
                continue;
            }

            QString themeId;
            if (const QString themeName = p.value("theme").toString(); !themeName.isEmpty() && themes.ok()) {
                for (const Theme &t : themes.value())
                    themeId = t.name == themeName ? t.id : themeId;
                if (themeId.isEmpty())
                    report.messages << QStringLiteral("%1: theme \"%2\" is not in this library; the default is used")
                                           .arg(p.value("title").toString(), themeName);
            }
            auto created = Repository<Presentation>(db).create({.libraryId = library,
                                                                .kind = *kind,
                                                                .title = p.value("title").toString(),
                                                                .themeId = themeId,
                                                                .metadata = p.value("metadata").toObject()});
            if (!created.ok())
                return created.error();
            presentationIds.insert(fileId, created.value().id);
            QHash<QString, QString> groupIds;
            int groupPosition = 0;
            for (const QJsonValue &g : p.value("groups").toArray()) {
                const QJsonObject group = g.toObject();
                auto newGroup = Repository<Group>(db).create({.presentationId = created.value().id,
                                                              .position = groupPosition++,
                                                              .label = group.value("label").toString(),
                                                              .colour = group.value("colour").toString(),
                                                              .hotkey = group.value("hotkey").toString()});
                if (!newGroup.ok())
                    return newGroup.error();
                groupIds.insert(group.value("id").toString(), newGroup.value().id);
                int slidePosition = 0;
                for (const QJsonValue &s : group.value("slides").toArray()) {
                    const QJsonObject slide = s.toObject();
                    auto newSlide = Repository<Slide>(db).create({.groupId = newGroup.value().id,
                                                                  .position = slidePosition++,
                                                                  .notes = slide.value("notes").toString(),
                                                                  .chords = slide.value("chords").toString(),
                                                                  .transition = slide.value("transition").toObject()});
                    if (!newSlide.ok())
                        return newSlide.error();
                    for (const QJsonValue &e : slide.value("elements").toArray()) {
                        const QJsonObject element = e.toObject();
                        const auto type = fromName(typeNames, element.value("type").toString());
                        if (!type)
                            return invalid(QStringLiteral("Unknown element type \"%1\"").arg(element.value("type").toString()));
                        const QJsonArray frame = element.value("frame").toArray();
                        QString asset;
                        if (const QString media = element.value("media").toString(); !media.isEmpty()) {
                            asset = assetIds.value(media);
                            if (asset.isEmpty())
                                return invalid(QStringLiteral("The file is incomplete: media %1 is missing").arg(media));
                        }
                        auto newElement = Repository<Element>(db).create(
                            {.slideId = newSlide.value().id,
                             .type = *type,
                             .frame = QRectF(frame.at(0).toDouble(), frame.at(1).toDouble(), frame.at(2).toDouble(), frame.at(3).toDouble()),
                             .style = element.value("style").toObject(),
                             .zOrder = element.value("z").toInt(),
                             .content = element.value("content").toString(),
                             .mediaAssetId = asset});
                        if (!newElement.ok())
                            return newElement.error();
                    }
                }
            }
            for (const QJsonValue &a : p.value("arrangements").toArray()) {
                const QJsonObject arrangement = a.toObject();
                QStringList groups;
                for (const QJsonValue &g : arrangement.value("groups").toArray())
                    groups << groupIds.value(g.toString());
                groups.removeAll(QString());
                auto newArrangement = Repository<Arrangement>(db).create({.presentationId = created.value().id,
                                                                          .name = arrangement.value("name").toString(),
                                                                          .isDefault = arrangement.value("default").toBool(),
                                                                          .groupIds = groups});
                if (!newArrangement.ok())
                    return newArrangement.error();
                arrangementIds.insert(arrangement.value("id").toString(), newArrangement.value().id);
            }
            ++report.presentationsAdded;
        }

        report.presentationIds = presentationIds.values();
        // The plan itself, always new (a file of songs only has none).
        if (!root.contains("plan"))
            return success();
        const QJsonObject plan = root.value("plan").toObject();
        report.planName = plan.value("name").toString(QStringLiteral("Imported plan"));
        auto newPlan = Repository<Plan>(db).create(
            {.name = report.planName, .date = QDate::fromString(plan.value("date").toString(), Qt::ISODate)});
        if (!newPlan.ok())
            return newPlan.error();
        report.planId = newPlan.value().id;
        int position = 0;
        for (const QJsonValue &i : plan.value("items").toArray()) {
            const QJsonObject item = i.toObject();
            const QString presentation = presentationIds.value(item.value("presentation").toString());
            if (presentation.isEmpty() && item.value("header").toString().isEmpty())
                continue;
            auto newItem = Repository<PlanItem>(db).create({.planId = newPlan.value().id,
                                                            .position = position++,
                                                            .presentationId = presentation,
                                                            .arrangementId = arrangementIds.value(item.value("arrangement").toString()),
                                                            .headerText = item.value("header").toString(),
                                                            .notes = item.value("notes").toString()});
            if (!newItem.ok())
                return newItem.error();
        }
        return success();
    });
    if (!done.ok()) {
        for (const QString &path : written)
            QFile::remove(path);
        return done.error();
    }
    return report;
}

} // namespace lumen::app::lumenfile
