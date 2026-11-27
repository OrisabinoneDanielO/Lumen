#pragma once

#include <QObject>
#include <QPointer>
#include <QTranslator>
#include <QVariantList>

#include <memory>

class QQmlEngine;

namespace lumen::app {

// The operator window's language (the design doc's localization). English is
// built in; the others are translations compiled into Lumen (i18n/*.ts).
// Changing it takes effect at once: the window is retranslated in place.
class Language : public QObject
{
    Q_OBJECT
    // {code, name (in that language), english (its English name)}
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(QString current READ current NOTIFY changed)

public:
    // draft: also offer languages whose translation is not finished yet
    // (setting experimental/draftLanguages).
    explicit Language(bool draft = false, QObject *parent = nullptr);
    ~Language() override;

    void setEngine(QQmlEngine *engine);
    QVariantList languages() const;
    QString current() const { return m_current; }
    // The code to start with: the saved choice, else the computer's
    // language if Lumen has it, else English.
    QString startLanguage(const QString &saved) const;
    bool available(const QString &code) const;

    // Switches to code ("en", "fr", ...); false if Lumen does not have it.
    Q_INVOKABLE bool choose(const QString &code);

signals:
    void changed();

private:
    bool m_draft;
    QString m_current = QStringLiteral("en");
    std::unique_ptr<QTranslator> m_lumen;
    std::unique_ptr<QTranslator> m_qt; // Qt's own words (dialog buttons)
    QPointer<QQmlEngine> m_engine;
};

} // namespace lumen::app
