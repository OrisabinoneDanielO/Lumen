#pragma once

#include "operatorcontroller.h"
#include "themes.h"

#include <QColor>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace lumen::app {

// The basic theme editor (Library > Themes): pick a theme, change its font,
// sizes, colours, alignment, background, safe area and footer with a live
// preview, then save. Saving updates every item that uses the theme.
class ThemeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList themes READ themes NOTIFY changed)
    Q_PROPERTY(int current READ current NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString appliesTo READ appliesTo NOTIFY changed)
    Q_PROPERTY(bool isDefault READ isDefault NOTIFY changed)
    Q_PROPERTY(QVariantMap style READ style NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(bool hasBackground READ hasBackground NOTIFY changed)
    Q_PROPERTY(QVariantMap previewStyle READ previewStyle NOTIFY changed)
    Q_PROPERTY(QString previewText READ previewText NOTIFY changed)
    Q_PROPERTY(QString previewFooter READ previewFooter NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QStringList fonts READ fonts CONSTANT)

public:
    explicit ThemeController(OperatorController &op, QObject *parent = nullptr);

    void attach(core::data::Database &db);

    QVariantList themes() const;
    int current() const { return m_current; }
    QString name() const { return m_edit.name; }
    void setName(const QString &name);
    QString appliesTo() const;
    bool isDefault() const { return m_edit.isDefault; }
    QVariantMap style() const { return m_edit.style.toJson().toVariantMap(); }
    QColor background() const;
    bool hasBackground() const { return background().isValid(); }
    QVariantMap previewStyle() const;
    QString previewText() const;
    QString previewFooter() const;
    bool dirty() const { return m_dirty; }
    QStringList fonts() const;

    const ThemeView &editing() const { return m_edit; }

    Q_INVOKABLE void select(int index);
    // Style fields by TextStyle JSON name ("font", "maxSize", "align", ...);
    // also "bold" (true/false) and "margin" (safe area inset, 0.02-0.15).
    Q_INVOKABLE void setStyleValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void setBackground(const QColor &colour); // invalid: none
    Q_INVOKABLE void makeDefault();
    Q_INVOKABLE bool save();
    Q_INVOKABLE void revert();
    Q_INVOKABLE bool duplicate(); // a new theme copied from this one

signals:
    void changed();

private:
    void reload(const QString &selectId = {});

    OperatorController &m_op;
    core::data::Database *m_db = nullptr;
    std::vector<ThemeView> m_themes;
    int m_current = -1;
    ThemeView m_edit;
    bool m_dirty = false;
};

} // namespace lumen::app
