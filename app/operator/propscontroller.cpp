#include "propscontroller.h"

#include "lumen/core/data/repository.h"
#include "lumen/core/show/showcontroller.h"

#include <QFileInfo>
#include <QUrl>

namespace lumen::app {

using namespace core::data;
using core::show::Content;
using core::show::Layer;

PropsController::PropsController(core::show::ShowController &show, QObject *parent) : QObject(parent), m_show(show)
{
    // Clear Props, Clear All or any other change of the layer shows here.
    connect(&m_show, &core::show::ShowController::sceneChanged, this, &PropsController::refresh);
}

void PropsController::attach(Database &db)
{
    m_db = &db;
    load();
}

void PropsController::load()
{
    m_props.clear();
    if (m_db) {
        if (auto all = Repository<Prop>(*m_db).list(); all.ok())
            m_props = all.value();
    }
    refresh();
}

QString PropsController::partName(PropKind kind)
{
    return kind == PropKind::Bug ? QStringLiteral("bug") : QStringLiteral("lowerThird");
}

QJsonObject PropsController::partFor(const Prop &prop) const
{
    QJsonObject part = prop.data;
    part.insert("id", prop.id);
    if (prop.kind == PropKind::Bug)
        part.insert("image", QUrl::fromLocalFile(prop.data.value("image").toString()).toString());
    return part;
}

void PropsController::refresh()
{
    const Content &layer = m_show.scene().layer(Layer::Props).content;
    QList<QVariantMap> rows;
    for (const Prop &p : m_props) {
        const bool shown = layer.kind == Content::Kind::Prop
                           && layer.prop.value(partName(p.kind)).toObject().value("id").toString() == p.id;
        const QString summary = p.kind == PropKind::Bug
                                    ? tr("Logo · %1").arg(QFileInfo(p.data.value("image").toString()).fileName())
                                    : p.data.value("line2").toString();
        rows << QVariantMap{{"name", p.name}, {"kind", partName(p.kind)}, {"summary", summary}, {"shown", shown}};
    }
    m_model.updateRows(rows);
}

bool PropsController::show(int row)
{
    if (row < 0 || row >= int(m_props.size()))
        return false;
    const Prop &prop = m_props[size_t(row)];
    const Content &layer = m_show.scene().layer(Layer::Props).content;
    // The other part (a bug under a new lower third, say) stays up.
    QJsonObject composed = layer.kind == Content::Kind::Prop ? layer.prop : QJsonObject();
    composed.insert(partName(prop.kind), partFor(prop));
    m_show.setContent(Layer::Props, Content::propContent(composed));
    return true;
}

bool PropsController::hide(int row)
{
    if (row < 0 || row >= int(m_props.size()))
        return false;
    const Prop &prop = m_props[size_t(row)];
    const Content &layer = m_show.scene().layer(Layer::Props).content;
    if (layer.kind != Content::Kind::Prop
        || layer.prop.value(partName(prop.kind)).toObject().value("id").toString() != prop.id)
        return false;
    QJsonObject composed = layer.prop;
    composed.remove(partName(prop.kind));
    m_show.setContent(Layer::Props, composed.isEmpty() ? Content::empty() : Content::propContent(composed));
    return true;
}

bool PropsController::toggle(int row)
{
    return m_model.get(row).value("shown").toBool() ? hide(row) : show(row);
}

bool PropsController::addLowerThird(const QString &line1, const QString &line2)
{
    if (!m_db || line1.trimmed().isEmpty())
        return false;
    auto created = Repository<Prop>(*m_db).create({.name = line1.trimmed(),
                                                   .kind = PropKind::LowerThird,
                                                   .data = {{"line1", line1.trimmed()}, {"line2", line2.trimmed()}}});
    if (!created.ok())
        return false;
    load();
    return true;
}

bool PropsController::addBug(const QUrl &image, const QString &corner)
{
    const QString path = image.isLocalFile() ? image.toLocalFile() : image.toString();
    static const QStringList corners{"topLeft", "topRight", "bottomLeft", "bottomRight"};
    if (!m_db || !QFileInfo(path).isFile() || !corners.contains(corner))
        return false;
    auto created = Repository<Prop>(*m_db).create({.name = QFileInfo(path).completeBaseName(),
                                                   .kind = PropKind::Bug,
                                                   .data = {{"image", QFileInfo(path).absoluteFilePath()},
                                                            {"corner", corner},
                                                            {"size", 0.12}}});
    if (!created.ok())
        return false;
    load();
    return true;
}

bool PropsController::remove(int row)
{
    if (!m_db || row < 0 || row >= int(m_props.size()))
        return false;
    hide(row); // off the screen first
    if (!Repository<Prop>(*m_db).remove(m_props[size_t(row)].id).ok())
        return false;
    load();
    return true;
}

} // namespace lumen::app
