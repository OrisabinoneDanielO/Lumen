#pragma once

#include "keymap.h"
#include "rowmodel.h"
#include "servicedata.h"

#include "lumen/core/show/showcontroller.h"
#include "lumen/core/songs/songlibrary.h"

#include <QObject>
#include <QUrl>
#include <QTimer>
#include <QVariantList>

namespace lumen::app {

// What the operator window shows and does. QML binds to its properties and
// calls its methods; every live change goes through the ShowController.
//
// Terms: the *selected* item is the one whose slides the grid shows. The
// *live* item and slide are what is on the Slide layer. *Preview* is the slide
// that Space (or "Next slide") will send live: the one after the live slide,
// or the selected item's first slide if that item is not live.
class OperatorController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *plan READ plan CONSTANT)
    Q_PROPERTY(QObject *slides READ slides CONSTANT)
    Q_PROPERTY(QObject *songs READ songs CONSTANT)
    Q_PROPERTY(QString planName READ planName NOTIFY stateChanged)
    Q_PROPERTY(QString planDetail READ planDetail NOTIFY stateChanged)
    Q_PROPERTY(int currentItem READ currentItem NOTIFY stateChanged)
    Q_PROPERTY(QString itemTitle READ itemTitle NOTIFY stateChanged)
    Q_PROPERTY(QString itemDetail READ itemDetail NOTIFY stateChanged)
    Q_PROPERTY(QVariantList arrangement READ arrangement NOTIFY stateChanged)
    Q_PROPERTY(int liveItem READ liveItem NOTIFY stateChanged)
    Q_PROPERTY(int liveSlide READ liveSlide NOTIFY stateChanged)       // in the grid, or -1
    Q_PROPERTY(int previewSlide READ previewSlide NOTIFY stateChanged) // in the grid, or -1
    Q_PROPERTY(int selectedSlide READ selectedSlide NOTIFY stateChanged) // Edit mode
    Q_PROPERTY(QString liveText READ liveText NOTIFY stateChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY stateChanged)
    Q_PROPERTY(QString liveLabel READ liveLabel NOTIFY stateChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY stateChanged)
    Q_PROPERTY(bool logoOn READ logoOn NOTIFY stateChanged)
    Q_PROPERTY(bool blackOn READ blackOn NOTIFY stateChanged)
    Q_PROPERTY(bool outputConnected READ outputConnected NOTIFY stateChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY stateChanged)
    Q_PROPERTY(bool editAvailable READ editAvailable CONSTANT)
    Q_PROPERTY(bool libraryAvailable READ libraryAvailable NOTIFY stateChanged)
    Q_PROPERTY(int selectedSong READ selectedSong NOTIFY stateChanged)
    Q_PROPERTY(QString importSummary READ importSummary NOTIFY stateChanged)
    Q_PROPERTY(QString importDetails READ importDetails NOTIFY stateChanged)

public:
    OperatorController(core::show::ShowController &show, bool editAvailable,
                       QObject *parent = nullptr);

    // Shows a plan (or none). Selects its first item; nothing goes live.
    void setPlan(std::optional<PlanView> plan);
    void setSongs(const std::vector<SongRow> &songs);
    void setOutputConnected(bool connected);
    // Turns on search, import, showing songs and Add to plan.
    void attachLibrary(core::data::Database &db, core::songs::SongLibrary &songs);

    QObject *plan() { return &m_planModel; }
    QObject *slides() { return &m_slideModel; }
    QObject *songs() { return &m_songModel; }
    QString planName() const;
    QString planDetail() const;
    int currentItem() const { return m_current; }
    QString itemTitle() const;
    QString itemDetail() const;
    QVariantList arrangement() const;
    int liveItem() const { return m_onAir ? m_positionItem : -1; }
    int liveSlide() const;
    int previewSlide() const;
    int selectedSlide() const { return m_selected; }
    QString liveText() const;
    QString previewText() const;
    QString liveLabel() const;
    QVariantList layers() const;
    bool logoOn() const { return m_show.scene().logo; }
    bool blackOn() const { return m_show.scene().black; }
    bool outputConnected() const { return m_outputConnected; }
    QString mode() const { return m_editMode ? QStringLiteral("edit") : QStringLiteral("show"); }
    void setMode(const QString &mode);
    bool editAvailable() const { return m_editAvailable; }
    bool libraryAvailable() const { return m_songs != nullptr; }
    int selectedSong() const { return m_selectedSong; }
    QString importSummary() const { return m_importSummary; }
    QString importDetails() const { return m_importDetails; }

    // The song list shows the songs matching query (all songs if empty).
    Q_INVOKABLE void searchSongs(const QString &query);
    // Shows a song from the list in the slide grid, ready to send live,
    // without adding it to the plan.
    Q_INVOKABLE bool showSong(int row);
    // Appends a song from the list to the plan (creating today's plan if
    // there is none).
    Q_INVOKABLE bool addSongToPlan(int row);
    // Appends any stored presentation to the plan (creating today's plan).
    bool addPresentationToPlan(const QString &presentationId);
    // Shows an item that is not in the plan or song list (e.g. a scripture
    // passage) in the slide grid, ready to send live.
    bool showItem(ItemView item);
    core::data::Database *database() const { return m_db; }
    // Imports song files and folders; returns the summary line.
    Q_INVOKABLE QString importSongs(const QList<QUrl> &urls);

    Q_INVOKABLE void selectItem(int index);
    // Show mode: sends a slide of the selected item live. Edit mode: selects
    // it for editing and sends nothing.
    Q_INVOKABLE void activateSlide(int index);
    Q_INVOKABLE bool goLive(int index);
    Q_INVOKABLE bool next();
    Q_INVOKABLE bool previous();
    Q_INVOKABLE void nextItem() { selectItem(m_current + 1); }
    Q_INVOKABLE void previousItem() { selectItem(m_current - 1); }
    Q_INVOKABLE bool jumpToGroup(const QString &labelPrefix);
    Q_INVOKABLE bool jumpToVerse(int number);
    Q_INVOKABLE bool jumpToSection(int number);
    Q_INVOKABLE void clearLayer(const QString &layerName);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void toggleLogo();
    Q_INVOKABLE void toggleBlack();

    // Routes a key press through the key map. Returns true if it was used.
    Q_INVOKABLE bool handleKey(int key, int modifiers);

signals:
    void stateChanged();
    void scriptureRequested(); // Ctrl+B

private:
    static constexpr int libraryItem = -2; // m_current when showing a library song

    const ItemView *item(int index) const;
    void showSelected();
    // Identifies an item across plan reloads and library browsing.
    static QString keyOf(const ItemView &item);
    // True if the live position belongs to the item the grid shows.
    bool positionIsSelected() const;
    const ItemView *selected() const { return item(m_current); }
    int firstSlideOfGroupFrom(const std::vector<int> &candidates) const;

    core::show::ShowController &m_show;
    bool m_editAvailable;
    bool m_editMode = false;
    bool m_outputConnected = false;

    core::data::Database *m_db = nullptr;
    core::songs::SongLibrary *m_songs = nullptr;
    QString m_search;
    int m_selectedSong = -1;
    QString m_importSummary;
    QString m_importDetails;
    std::optional<ItemView> m_librarySong;

    std::optional<PlanView> m_plan;
    int m_current = -1;
    int m_selected = 0;

    // The slide last sent live (kept after Clear Slide, so Space carries on).
    int m_positionItem = -1;
    QString m_positionKey; // which item that was (the grid slot may change)
    int m_positionSlide = -1;
    QString m_positionLabel; // "Verse 2 · slide 3", fixed when it went live
    bool m_onAir = false;

    KeyMap m_keys;
    bool m_versePending = false;
    QTimer m_verseTimeout;

    RowModel m_planModel{{"kind", "title", "subtitle"}};
    RowModel m_slideModel{{"group", "colour", "text"}};
    RowModel m_songModel{{"title", "author", "copyright", "key", "presentationId"}};
};

} // namespace lumen::app
