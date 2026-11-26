#include "screensetup.h"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QRect>
#include <QScreen>
#include <QSettings>

#include <algorithm>

namespace lumen::app {

namespace {

constexpr auto setupsKey = "screens/setups";

QString screenName(const QScreen *screen)
{
    const QString made = (screen->manufacturer() + QLatin1Char(' ') + screen->model()).trimmed();
    return made.isEmpty() ? screen->name() : made;
}

QString roleText(const QString &role)
{
    if (role == QLatin1String("operator"))
        return QObject::tr("This screen: the operator window");
    if (role == QLatin1String("main"))
        return QObject::tr("Main output");
    if (role == QLatin1String("stage"))
        return QObject::tr("Stage display");
    return QObject::tr("Not used");
}

} // namespace

ScreenSetup::ScreenSetup(QObject *parent) : QObject(parent)
{
    refresh();
}

QString ScreenSetup::screenKey(const QScreen *screen)
{
    // The connector name, the monitor and its size: the same monitor on the
    // same connector is the same screen next time.
    return QStringLiteral("%1|%2|%3|%4x%5")
        .arg(screen->name(), screenName(screen), screen->serialNumber())
        .arg(screen->size().width())
        .arg(screen->size().height());
}

QString ScreenSetup::setKey(const QStringList &screenKeys)
{
    QStringList sorted = screenKeys;
    std::sort(sorted.begin(), sorted.end());
    return sorted.join(QLatin1String(" + "));
}

void ScreenSetup::refresh()
{
    const int mainBefore = mainScreen();
    const int stageBefore = stageScreen();
    m_screens = QGuiApplication::screens();
    m_keys.clear();
    for (const QScreen *screen : m_screens)
        m_keys << screenKey(screen);
    defaultRoles();
    restore();
    publish();
    if (mainScreen() != mainBefore || stageScreen() != stageBefore)
        emit rolesChanged();
}

int ScreenSetup::operatorIndex() const
{
    const int primary = int(m_screens.indexOf(QGuiApplication::primaryScreen()));
    return primary >= 0 ? primary : 0;
}

void ScreenSetup::defaultRoles()
{
    // As before there was a setup: the output on the first screen that is not
    // the operator's, nothing on the others.
    m_roles.clear();
    const int op = operatorIndex();
    bool mainGiven = false;
    for (int i = 0; i < m_screens.size(); ++i) {
        if (i == op) {
            m_roles << QStringLiteral("operator");
        } else if (!mainGiven) {
            m_roles << QStringLiteral("main");
            mainGiven = true;
        } else {
            m_roles << QStringLiteral("none");
        }
    }
}

void ScreenSetup::restore()
{
    const QJsonObject setups = QJsonDocument::fromJson(QSettings().value(QLatin1String(setupsKey)).toByteArray()).object();
    const QJsonObject saved = setups.value(setKey(m_keys)).toObject();
    if (saved.isEmpty())
        return;
    for (int i = 0; i < m_screens.size(); ++i) {
        if (m_roles.at(i) == QLatin1String("operator"))
            continue;
        const QString role = saved.value(m_keys.at(i)).toString();
        if (role == QLatin1String("main") || role == QLatin1String("stage") || role == QLatin1String("none"))
            m_roles[i] = role;
    }
}

void ScreenSetup::save() const
{
    QSettings settings;
    QJsonObject setups = QJsonDocument::fromJson(settings.value(QLatin1String(setupsKey)).toByteArray()).object();
    QJsonObject roles;
    for (int i = 0; i < m_screens.size(); ++i)
        roles.insert(m_keys.at(i), m_roles.at(i));
    setups.insert(setKey(m_keys), roles);
    settings.setValue(QLatin1String(setupsKey), QJsonDocument(setups).toJson(QJsonDocument::Compact));
}

bool ScreenSetup::needsSetup() const
{
    if (m_screens.size() < 2)
        return false;
    const QJsonObject setups = QJsonDocument::fromJson(QSettings().value(QLatin1String(setupsKey)).toByteArray()).object();
    return !setups.contains(setKey(m_keys));
}

QString ScreenSetup::role(int index) const
{
    return index >= 0 && index < m_roles.size() ? m_roles.at(index) : QString();
}

int ScreenSetup::indexOf(const QString &role) const
{
    return int(m_roles.indexOf(role));
}

void ScreenSetup::setRole(int index, const QString &role)
{
    if (index < 0 || index >= m_roles.size() || m_roles.at(index) == QLatin1String("operator"))
        return;
    if (role != QLatin1String("main") && role != QLatin1String("stage") && role != QLatin1String("none"))
        return;
    if (m_roles.at(index) == role)
        return;
    if (role != QLatin1String("none")) {
        const int had = indexOf(role);
        if (had >= 0)
            m_roles[had] = QStringLiteral("none");
    }
    m_roles[index] = role;
    publish();
    emit rolesChanged();
}

void ScreenSetup::openSetup()
{
    if (m_open)
        return;
    m_open = true;
    publish();
    emit openChanged();
}

void ScreenSetup::close()
{
    save();
    if (!m_open)
        return;
    m_open = false;
    publish();
    emit openChanged();
}

void ScreenSetup::setRunning(bool mainRunning, bool stageRunning)
{
    if (mainRunning == m_mainRunning && stageRunning == m_stageRunning)
        return;
    m_mainRunning = mainRunning;
    m_stageRunning = stageRunning;
    publish();
}

QJsonObject ScreenSetup::label(int index) const
{
    if (index < 0 || index >= m_screens.size())
        return {};
    return {{QStringLiteral("number"), index + 1},
            {QStringLiteral("name"), screenName(m_screens.at(index))},
            {QStringLiteral("role"), roleText(m_roles.at(index))}};
}

void ScreenSetup::publish()
{
    // The arrangement, scaled so the whole fits in 0..1.
    QRect all;
    for (const QScreen *screen : m_screens)
        all = all.united(screen->geometry());
    const double scale = std::max(1, std::max(all.width(), all.height()));
    m_spanX = all.width() / scale;
    m_spanY = all.height() / scale;

    QList<QVariantMap> rows;
    for (int i = 0; i < m_screens.size(); ++i) {
        const QScreen *screen = m_screens.at(i);
        const QRect g = screen->geometry();
        const QString role = m_roles.at(i);
        // A renderer draws the label on its own screen; the operator window
        // draws it everywhere else (except on its own screen, where the
        // setup itself says which screen this is).
        const bool rendered = (role == QLatin1String("main") && m_mainRunning)
                              || (role == QLatin1String("stage") && m_stageRunning);
        rows << QVariantMap{{"index", i},
                            {"number", i + 1},
                            {"name", screenName(screen)},
                            {"size", QStringLiteral("%1 × %2").arg(screen->size().width()).arg(screen->size().height())},
                            {"role", role},
                            {"operator", role == QLatin1String("operator")},
                            {"labelHere", m_open && !rendered && role != QLatin1String("operator")},
                            {"areaX", (g.x() - all.x()) / scale},
                            {"areaY", (g.y() - all.y()) / scale},
                            {"areaW", g.width() / scale},
                            {"areaH", g.height() / scale}};
    }
    m_rows.updateRows(rows);
    emit changed();
}

} // namespace lumen::app
