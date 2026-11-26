#pragma once

#include "rowmodel.h"

#include "lumen/core/data/database.h"
#include "lumen/core/data/entities.h"

#include <QJsonArray>
#include <QObject>
#include <QRectF>
#include <QUrl>
#include <QVariantMap>

#include <vector>

namespace lumen::core::songs {
class SongLibrary;
}

namespace lumen::app {

class OperatorController;

// The slide editor (batch 16), behind the experimental/editor setting.
//
// It edits a draft of one presentation; nothing is stored, and nothing
// reaches the screen, until Save (Cancel throws the draft away). Save writes
// the whole presentation in one transaction and keeps its id, so plans that
// use it carry on.
//
// Slides presentations take elements: text boxes, shapes, pictures, videos,
// clocks and countdowns, each placed on a 1920 x 1080 grid and styled; every
// element made here carries a style, which is what makes its slide drawn as
// composed content (servicedata.h). Songs are edited as lyrics text: a blank
// line starts a new slide and a line like [Verse 2] starts a group; saving
// splits them again and rebuilds the default arrangement.
class EditorController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    Q_PROPERTY(QString presentationId READ presentationId NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY changed)
    Q_PROPERTY(bool isSong READ isSong NOTIFY changed)
    Q_PROPERTY(bool canInsert READ canInsert NOTIFY changed)
    Q_PROPERTY(QString lyrics READ lyrics WRITE setLyrics NOTIFY changed)
    // A song's play order (group labels, repeats allowed), the groups its
    // lyrics have, and whether the order was set by hand (else it follows
    // the lyrics as written).
    Q_PROPERTY(QStringList arrangement READ arrangement NOTIFY changed)
    Q_PROPERTY(QStringList groupLabels READ groupLabels NOTIFY changed)
    Q_PROPERTY(bool arrangementCustom READ arrangementCustom NOTIFY changed)
    Q_PROPERTY(QObject *slides READ slides CONSTANT)
    Q_PROPERTY(int slide READ slide WRITE selectSlide NOTIFY changed)
    Q_PROPERTY(QVariantList elements READ elements NOTIFY changed)
    Q_PROPERTY(int element READ element WRITE selectElement NOTIFY changed)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY changed)
    Q_PROPERTY(QString transition READ transition NOTIFY changed)
    Q_PROPERTY(QObject *themes READ themes CONSTANT)
    Q_PROPERTY(int theme READ theme NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)

public:
    explicit EditorController(OperatorController &op, QObject *parent = nullptr);

    void attach(core::data::Database &db, core::songs::SongLibrary *songs);

    bool isOpen() const { return !m_presentation.id.isEmpty(); }
    QString presentationId() const { return m_presentation.id; }
    bool dirty() const { return m_dirty; }
    QString title() const { return m_presentation.title; }
    void setTitle(const QString &title);
    bool isSong() const { return m_presentation.kind == core::data::PresentationKind::Song; }
    bool canInsert() const { return isOpen() && m_presentation.kind == core::data::PresentationKind::Slides; }
    QString lyrics() const { return m_lyrics; }
    QStringList arrangement() const;
    QStringList groupLabels() const;
    bool arrangementCustom() const { return m_arrangementCustom; }
    void setLyrics(const QString &lyrics);
    QObject *slides() { return &m_slideModel; }
    int slide() const { return m_slide; }
    void selectSlide(int index);
    QVariantList elements() const;
    int element() const { return m_element; }
    void selectElement(int index);
    QVariantMap selected() const;
    QString transition() const;
    QObject *themes() { return &m_themeModel; }
    int theme() const;
    QString message() const { return m_message; }

    // Opens a presentation as a draft (throwing away any unsaved one).
    Q_INVOKABLE bool openPresentation(const QString &presentationId);
    // The item selected in the operator window.
    Q_INVOKABLE bool openSelected();
    // A new slides presentation with one empty slide, added to the plan and
    // opened.
    Q_INVOKABLE bool newPresentation(const QString &title);

    // type: text, shape, image, video, clock, timer. Pictures and videos
    // need a file. The new element is selected.
    Q_INVOKABLE bool addElement(const QString &type, const QUrl &media = {});
    Q_INVOKABLE bool removeElement();
    Q_INVOKABLE void setFrame(int index, qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void setText(const QString &text);
    Q_INVOKABLE void setStyle(const QString &key, const QVariant &value);
    Q_INVOKABLE void setTimerName(const QString &name);
    Q_INVOKABLE void raise();
    Q_INVOKABLE void lower();

    Q_INVOKABLE void addSlide();
    Q_INVOKABLE void duplicateSlide();
    Q_INVOKABLE bool removeSlide();
    // "theme" (the layer's default), "cut" or "fade".
    Q_INVOKABLE void setTransition(const QString &kind);
    // A picture or motion loop filling the slide, under everything else.
    Q_INVOKABLE bool setBackground(const QUrl &media);
    Q_INVOKABLE void setTheme(int row); // -1: no theme of its own

    // Songs: each group's lines in even slides of "lines per slide"
    // (songs/linesPerSlide), in the lyrics.
    Q_INVOKABLE void autoSplit();
    Q_INVOKABLE void addToArrangement(const QString &label);
    Q_INVOKABLE void removeFromArrangement(int index);
    Q_INVOKABLE void moveInArrangement(int from, int to);
    Q_INVOKABLE void resetArrangement(); // back to the lyrics as written
    Q_INVOKABLE bool save();
    Q_INVOKABLE void cancel();

    // Lyrics as the editor shows them, from stored groups and slides.
    static QString lyricsOf(const std::vector<std::pair<QString, QStringList>> &groups);
    // The editor's lyrics rules (blank line: new slide in the group) in the
    // song importer's terms ("---" between slides).
    static QString toSongText(const QString &lyrics);

signals:
    void changed();

private:
    struct DraftElement
    {
        core::data::ElementType type = core::data::ElementType::Text;
        QRectF frame;
        QJsonObject style;
        QString content;      // text, or a timer's name
        QString mediaPath;    // pictures and videos
        QString mediaAssetId; // when already in the library
    };
    struct DraftSlide
    {
        QString group;
        QJsonObject transition;
        QString notes;
        QString chords;
        std::vector<DraftElement> elements; // bottom first
    };

    void load(const QString &presentationId);
    void touch(); // a change: dirty, models refreshed
    void refresh();
    DraftSlide *current();
    DraftElement *currentElement();
    QJsonArray elementsJson(const DraftSlide &slide) const;
    core::Result<QString> assetFor(const DraftElement &element, const QString &libraryId);
    core::Status saveSlides();
    core::Status saveSong();

    OperatorController &m_op;
    core::data::Database *m_db = nullptr;
    core::songs::SongLibrary *m_songs = nullptr;
    core::data::Presentation m_presentation;
    std::vector<DraftSlide> m_draft;
    QString m_lyrics;
    bool m_lyricsChanged = false;
    QStringList m_arrangement; // set by hand (m_arrangementCustom)
    bool m_arrangementCustom = false;
    bool m_arrangementChanged = false;
    QStringList writtenOrder() const; // play order from the lyrics
    void arrangementEdited();
    core::Status saveArrangement(const QHash<QString, QString> &groupIds, const QStringList &order);
    bool m_dirty = false;
    int m_slide = 0;
    int m_element = -1;
    QString m_message;
    std::vector<core::data::Theme> m_themes;
    RowModel m_slideModel{{"group", "text", "elements"}};
    RowModel m_themeModel{{"name"}};
};

} // namespace lumen::app
