#include "language.h"

#include <QCoreApplication>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlEngine>

namespace lumen::app {

namespace {

struct Known
{
    const char *code;
    const char *name;    // in its own language
    const char *english;
    bool finished;       // false: a draft, offered only with draftLanguages
};

// The design doc's first wave. Yoruba, Igbo, Hausa and Swahili wait for
// native speakers (see i18n/README.md).
constexpr Known known[] = {
    {"en", "English", "English", true},
    {"fr", "Français", "French", true},
    {"es", "Español", "Spanish", true},
    {"pt", "Português", "Portuguese", true},
    {"yo", "Yorùbá", "Yoruba", false},
    {"ig", "Igbo", "Igbo", false},
    {"ha", "Hausa", "Hausa", false},
    {"sw", "Kiswahili", "Swahili", false},
};

QString resourceFor(const QString &code)
{
    return QStringLiteral(":/i18n/lumen_%1.qm").arg(code);
}

} // namespace

Language::Language(bool draft, QObject *parent) : QObject(parent), m_draft(draft) {}

void Language::setEngine(QQmlEngine *engine)
{
    m_engine = engine;
}

Language::~Language()
{
    if (m_lumen)
        QCoreApplication::removeTranslator(m_lumen.get());
    if (m_qt)
        QCoreApplication::removeTranslator(m_qt.get());
}

bool Language::available(const QString &code) const
{
    for (const Known &k : known) {
        if (code == QLatin1String(k.code))
            return (k.finished || m_draft) && (code == QLatin1String("en") || QFile::exists(resourceFor(code)));
    }
    return false;
}

QVariantList Language::languages() const
{
    QVariantList list;
    for (const Known &k : known) {
        const QString code = QString::fromLatin1(k.code);
        if (available(code))
            list << QVariantMap{{"code", code}, {"name", QString::fromUtf8(k.name)}, {"english", QString::fromUtf8(k.english)}};
    }
    return list;
}

QString Language::startLanguage(const QString &saved) const
{
    if (!saved.isEmpty() && available(saved))
        return saved;
    const QString system = QLocale::system().name().section(u'_', 0, 0);
    return available(system) ? system : QStringLiteral("en");
}

bool Language::choose(const QString &code)
{
    if (!available(code))
        return false;
    if (m_lumen) {
        QCoreApplication::removeTranslator(m_lumen.get());
        m_lumen.reset();
    }
    if (m_qt) {
        QCoreApplication::removeTranslator(m_qt.get());
        m_qt.reset();
    }
    if (code != QLatin1String("en")) {
        auto lumen = std::make_unique<QTranslator>();
        if (!lumen->load(resourceFor(code)))
            return false;
        QCoreApplication::installTranslator(lumen.get());
        m_lumen = std::move(lumen);
        // Qt's own words, where Qt's translations are installed.
        auto qt = std::make_unique<QTranslator>();
        if (qt->load(QLocale(code), QStringLiteral("qtbase"), QStringLiteral("_"),
                     QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            QCoreApplication::installTranslator(qt.get());
            m_qt = std::move(qt);
        }
    }
    m_current = code;
    if (m_engine)
        m_engine->retranslate();
    emit changed();
    return true;
}

} // namespace lumen::app
