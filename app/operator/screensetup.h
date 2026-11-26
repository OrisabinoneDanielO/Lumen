#pragma once

#include "rowmodel.h"

#include <QJsonObject>
#include <QObject>
#include <QStringList>

class QScreen;

namespace lumen::app {

// The screen setup: which connected screen shows the audience output (the
// "main" role), which the stage display ("stage"), and which nothing
// ("none"). The operator's screen (the primary) keeps the operator window.
//
// While the setup is open every screen shows a large label with its number,
// name and role (drawn by the renderer on its screens and by the operator
// window on the others), so the operator can tell them apart.
//
// Each set of screens remembers its roles (QSettings "screens/setups"), so the
// outputs go back to the right screens at the next start. A set of screens
// seen for the first time needs setting up: the setup opens by itself.
class ScreenSetup : public QObject
{
    Q_OBJECT
    // Rows: number, name, size, role, operator (bool), labelHere (bool: the
    // operator window draws this screen's label), and areaX, areaY, areaW,
    // areaH: where the screen is in the arrangement, as fractions of the whole.
    Q_PROPERTY(QObject *screens READ screens CONSTANT)
    Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
    Q_PROPERTY(int count READ count NOTIFY changed)
    // The whole arrangement's width and height, in the rows' fractions.
    Q_PROPERTY(double spanX READ spanX NOTIFY changed)
    Q_PROPERTY(double spanY READ spanY NOTIFY changed)

public:
    explicit ScreenSetup(QObject *parent = nullptr);

    QObject *screens() { return &m_rows; }
    bool isOpen() const { return m_open; }
    int count() const { return int(m_screens.size()); }
    double spanX() const { return m_spanX; }
    double spanY() const { return m_spanY; }

    // Indexes into QGuiApplication::screens(), or -1 when no screen has the role.
    int mainScreen() const { return indexOf(QStringLiteral("main")); }
    int stageScreen() const { return indexOf(QStringLiteral("stage")); }
    QString role(int index) const;

    // More than one screen, and this set of screens has not been set up.
    bool needsSetup() const;

    // Gives screen `index` a role ("main", "stage" or "none"). A screen has
    // one role and a role one screen: the screen that had it is left unused.
    Q_INVOKABLE void setRole(int index, const QString &role);
    Q_INVOKABLE void openSetup();
    // Closes the setup and remembers the roles for this set of screens.
    Q_INVOKABLE void close();

    // The label for screen `index`: {number, name, role}.
    QJsonObject label(int index) const;

    // Which outputs are running, so the operator window draws the labels
    // of the screens no renderer is on.
    void setRunning(bool mainRunning, bool stageRunning);

    // Reads the connected screens again (one was added or removed).
    void refresh();

    // Stable names for a screen and a set of screens, for the settings.
    static QString screenKey(const QScreen *screen);
    static QString setKey(const QStringList &screenKeys);

signals:
    void changed();
    void openChanged();
    // Main or stage moved to another screen (or to none).
    void rolesChanged();

private:
    int indexOf(const QString &role) const;
    int operatorIndex() const;
    void defaultRoles();
    void restore();
    void save() const;
    void publish();

    QList<QScreen *> m_screens;
    QStringList m_keys;
    QStringList m_roles;
    bool m_open = false;
    bool m_mainRunning = false;
    bool m_stageRunning = false;
    double m_spanX = 1;
    double m_spanY = 1;
    RowModel m_rows{{"index", "number", "name", "size", "role", "operator", "labelHere", "areaX", "areaY", "areaW", "areaH"}};
};

} // namespace lumen::app
