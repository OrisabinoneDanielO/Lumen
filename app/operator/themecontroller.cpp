#include "themecontroller.h"

namespace lumen::app {

using namespace core;
using core::data::ThemeTarget;
using core::show::TextStyle;

namespace {

QString targetName(ThemeTarget target)
{
    switch (target) {
    case ThemeTarget::Song: return QStringLiteral("song");
    case ThemeTarget::Scripture: return QStringLiteral("scripture");
    case ThemeTarget::LowerThird: return QStringLiteral("lower_third");
    case ThemeTarget::Slides: return QStringLiteral("slides");
    case ThemeTarget::Media: return QStringLiteral("media");
    case ThemeTarget::Any: break;
    }
    return QStringLiteral("any");
}

// Sample text the preview shows for each kind of theme.
struct Sample
{
    QString text;
    QString footer;
};

Sample sampleFor(ThemeTarget target)
{
    switch (target) {
    case ThemeTarget::Scripture:
        return {QStringLiteral("We know that all things work together for good for those who love God, for those "
                               "who are called according to his purpose."),
                QStringLiteral("Romans 8:28 (WEB) · World English Bible. Public domain.")};
    case ThemeTarget::LowerThird:
        return {QStringLiteral("Pastor Ade Johnson"), QString()};
    default:
        return {QStringLiteral("Amazing grace! How sweet the sound\nThat saved a wretch like me!"),
                QStringLiteral("Amazing Grace · John Newton · Public domain")};
    }
}

} // namespace

ThemeController::ThemeController(OperatorController &op, QObject *parent) : QObject(parent), m_op(op) {}

void ThemeController::attach(data::Database &db)
{
    m_db = &db;
    reload();
}

void ThemeController::reload(const QString &selectId)
{
    m_themes.clear();
    if (m_db) {
        if (auto list = listThemes(*m_db); list.ok())
            m_themes = list.value();
    }
    int index = m_themes.empty() ? -1 : 0;
    for (std::size_t i = 0; i < m_themes.size(); ++i) {
        if (m_themes[i].id == selectId)
            index = int(i);
    }
    m_current = -1;
    select(index);
    emit changed();
}

QVariantList ThemeController::themes() const
{
    QVariantList list;
    for (const ThemeView &theme : m_themes) {
        list << QVariantMap{{"id", theme.id},
                            {"name", theme.name},
                            {"appliesTo", targetName(theme.appliesTo)},
                            {"isDefault", theme.isDefault}};
    }
    return list;
}

void ThemeController::select(int index)
{
    if (index < 0 || index >= int(m_themes.size())) {
        m_current = -1;
        m_edit = {};
    } else {
        m_current = index;
        m_edit = m_themes[std::size_t(index)];
    }
    m_dirty = false;
    emit changed();
}

void ThemeController::setName(const QString &name)
{
    if (m_current < 0 || name == m_edit.name || name.trimmed().isEmpty())
        return;
    m_edit.name = name.trimmed();
    m_dirty = true;
    emit changed();
}

QString ThemeController::appliesTo() const
{
    return targetName(m_edit.appliesTo);
}

void ThemeController::setStyleValue(const QString &key, const QVariant &value)
{
    if (m_current < 0)
        return;
    QJsonObject json = m_edit.style.toJson();
    if (key == QLatin1String("bold")) {
        json.insert(QStringLiteral("weight"), value.toBool() ? 700 : 400);
    } else if (key == QLatin1String("margin")) {
        const double inset = qBound(0.02, value.toDouble(), 0.15);
        const QRectF current = m_edit.style.safeArea;
        // Keep a lower third's vertical band; inset everything else evenly.
        QRectF area = m_edit.appliesTo == ThemeTarget::LowerThird
                          ? QRectF(inset, current.y(), 1 - 2 * inset, current.height())
                          : QRectF(inset, inset, 1 - 2 * inset, 1 - 2 * inset);
        json.insert(QStringLiteral("safeArea"), QJsonObject{{"x", area.x()}, {"y", area.y()},
                                                            {"w", area.width()}, {"h", area.height()}});
    } else if (value.typeId() == QMetaType::QColor) {
        json.insert(key, value.value<QColor>().name(QColor::HexArgb));
    } else {
        json.insert(key, QJsonValue::fromVariant(value));
    }
    const TextStyle updated = TextStyle::fromJson(json);
    if (updated == m_edit.style)
        return;
    m_edit.style = updated;
    m_dirty = true;
    emit changed();
}

QColor ThemeController::background() const
{
    return QColor::fromString(m_edit.background.value(QStringLiteral("colour")).toString());
}

void ThemeController::setBackground(const QColor &colour)
{
    if (m_current < 0)
        return;
    // Invalid or fully transparent: no background (the layers below show).
    const bool none = !colour.isValid() || colour.alpha() == 0;
    const QJsonObject background = none ? QJsonObject{} : QJsonObject{{"colour", colour.name()}};
    if (background == m_edit.background)
        return;
    m_edit.background = background;
    m_dirty = true;
    emit changed();
}

void ThemeController::makeDefault()
{
    if (m_current < 0 || m_edit.isDefault)
        return;
    m_edit.isDefault = true;
    m_dirty = true;
    emit changed();
}

bool ThemeController::save()
{
    if (!m_db || m_current < 0)
        return false;
    auto saved = saveTheme(*m_db, m_edit);
    if (!saved.ok()) {
        qWarning("Theme not saved: %s", qPrintable(saved.error().message));
        return false;
    }
    m_op.refreshThemes();
    reload(saved.value().id);
    return true;
}

void ThemeController::revert()
{
    select(m_current);
}

bool ThemeController::duplicate()
{
    if (!m_db || m_current < 0)
        return false;
    ThemeView copy = m_edit;
    copy.id.clear();
    copy.name = m_edit.name + tr(" (copy)");
    copy.isDefault = false;
    auto saved = saveTheme(*m_db, copy);
    if (!saved.ok())
        return false;
    reload(saved.value().id);
    return true;
}

QVariantMap ThemeController::previewStyle() const
{
    if (m_current < 0)
        return {};
    const Sample sample = sampleFor(m_edit.appliesTo);
    const int size = show::fitText(sample.text, m_edit.style, !sample.footer.isEmpty()).pixelSize;
    return styleWithSize(m_edit.style, size).toVariantMap();
}

QString ThemeController::previewText() const
{
    return sampleFor(m_edit.appliesTo).text;
}

QString ThemeController::previewFooter() const
{
    return sampleFor(m_edit.appliesTo).footer;
}

QStringList ThemeController::fonts() const
{
    return {QStringLiteral("Noto Sans"), QStringLiteral("Noto Serif")};
}

} // namespace lumen::app
