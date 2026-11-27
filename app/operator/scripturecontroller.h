#pragma once

#include "operatorcontroller.h"
#include "servicedata.h"

#include "lumen/core/scripture/biblestore.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <optional>
#include <vector>

namespace lumen::app {

// Scripture quick search (Ctrl+B), after design/mockups/Scripture.html: type a
// reference, pick a translation, then Show now (Enter) or Add to plan. Each
// slide carries the reference and translation; the translation's copyright
// line is shown with it.
class ScriptureController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QVariantList translations READ translations NOTIFY changed)
    Q_PROPERTY(int translation READ translation WRITE setTranslation NOTIFY changed)
    Q_PROPERTY(QString query READ query NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)        // "John 3:16–18"
    Q_PROPERTY(QString error READ error NOTIFY changed)        // why nothing was found
    Q_PROPERTY(QVariantList verses READ verses NOTIFY changed) // {number, text}
    // 1 or 2 verses on each slide, or 0: as many as fit.
    Q_PROPERTY(int versesPerSlide READ versesPerSlide WRITE setVersesPerSlide NOTIFY changed)
    // Where the reference goes: "below", "above" or "lowerThird" (the text
    // and reference in the bottom third; see scriptureStyle).
    Q_PROPERTY(QString referencePosition READ referencePosition WRITE setReferencePosition NOTIFY changed)
    // A second translation side by side (an index in translations), or -1.
    Q_PROPERTY(int secondTranslation READ secondTranslation WRITE setSecondTranslation NOTIFY changed)
    Q_PROPERTY(QString previewSecondText READ previewSecondText NOTIFY changed)
    // The copyright line on the slides. Off: left out for public-domain
    // Bibles; Bibles whose licence asks for credit (CC BY-SA) keep theirs.
    Q_PROPERTY(bool showCopyright READ showCopyright WRITE setShowCopyright NOTIFY changed)
    // How many verses the first slide holds (highlighted in the list).
    Q_PROPERTY(int firstSlideVerses READ firstSlideVerses NOTIFY changed)
    Q_PROPERTY(QString copyright READ copyright NOTIFY changed)
    Q_PROPERTY(QString previewText READ previewText NOTIFY changed)
    Q_PROPERTY(QString previewReference READ previewReference NOTIFY changed)
    Q_PROPERTY(QString addSummary READ addSummary NOTIFY changed)
    // Typing ahead (see completeReference): the passage the typing most
    // likely leads to, and what Space turns the typing into.
    Q_PROPERTY(QString suggestion READ suggestion NOTIFY changed)
    Q_PROPERTY(QString completionStep READ completionStep NOTIFY changed)

public:
    explicit ScriptureController(OperatorController &op, QObject *parent = nullptr);

    // The bundled Bibles (read-only) and the user's added ones; either may be
    // null.
    void setStores(core::scripture::BibleStore *bundled, core::scripture::BibleStore *user);

    bool available() const { return !m_sources.empty(); }
    QVariantList translations() const;
    int translation() const { return m_translation; }
    void setTranslation(int index);
    QString query() const { return m_query; }
    QString label() const { return m_passage ? m_passage->label : QString(); }
    QString error() const { return m_error; }
    QVariantList verses() const;
    int versesPerSlide() const { return m_versesPerSlide; }
    void setVersesPerSlide(int count);
    QString referencePosition() const { return m_referencePosition; }
    void setReferencePosition(const QString &position);
    int secondTranslation() const { return m_second; }
    void setSecondTranslation(int index);
    QString previewSecondText() const;
    int firstSlideVerses() const { return m_firstSlideVerses; }
    bool showCopyright() const { return m_showCopyright; }
    void setShowCopyright(bool show);
    QString copyright() const;
    QString previewText() const;
    QString previewReference() const;
    QString addSummary() const { return m_addSummary; }
    QString suggestion() const { return m_completion.suggestion; }
    QString completionStep() const { return m_completion.step; }

    // The passage as slides: one or two verses each, or as many as fit,
    // themed, with the reference and copyright in the footer. Verses too
    // long for one slide even at the theme's smallest size are split across
    // slides. With a second translation, each slide is two columns.
    const ItemView &item() const { return m_item; }

    Q_INVOKABLE void lookup(const QString &text);
    // The book a typing means, by its name in the chosen translation ("j" ->
    // "John", "1 c" -> "1 Corinthians"); empty if none.
    Q_INVOKABLE QString bookFor(const QString &typed) const;
    Q_INVOKABLE void selectTranslation(int index) { setTranslation(index); }
    // Sends the first slide live and shows the passage in the slide grid.
    Q_INVOKABLE bool showNow();
    // The passage in the slide grid and Preview, not yet on the screen.
    Q_INVOKABLE bool preview();
    // Stores the passage as a scripture presentation and appends it to the plan.
    Q_INVOKABLE bool addToPlan();
    // Adds a Bible from a folder of USFM files or a USFX file.
    Q_INVOKABLE QString addBible(const QUrl &location);

signals:
    void changed();

private:
    core::scripture::Completion m_completion;
    struct Source
    {
        core::scripture::BibleStore *store;
        core::scripture::Translation translation;
    };

    void reload();
    void refresh();
    void rebuild();

    OperatorController &m_op;
    core::scripture::BibleStore *m_bundled = nullptr;
    core::scripture::BibleStore *m_user = nullptr;
    std::vector<Source> m_sources;
    core::scripture::BookNames m_names;

    int m_translation = 0;
    QString m_query;
    QString m_error;
    int m_versesPerSlide = 1;
    QString m_referencePosition = QStringLiteral("below");
    int m_second = -1;
    int m_firstSlideVerses = 0;
    bool m_showCopyright = true;
    // The copyright lines the slides carry (see showCopyright), joined.
    QString credit() const;
    std::optional<core::scripture::Passage> m_passage;
    std::optional<core::scripture::Passage> m_secondPassage; // the same verses
    ItemView m_item;
    QString m_addSummary;
};

} // namespace lumen::app
