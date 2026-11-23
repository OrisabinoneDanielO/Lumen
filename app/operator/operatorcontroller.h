#pragma once

#include "keymap.h"
#include "medialibrary.h"
#include "presentationlibrary.h"
#include "backups.h"
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
    Q_PROPERTY(QObject *media READ media CONSTANT)
    Q_PROPERTY(int selectedMedia READ selectedMedia NOTIFY stateChanged)
    Q_PROPERTY(QString mediaSummary READ mediaSummary NOTIFY stateChanged)
    Q_PROPERTY(bool mediaAvailable READ mediaAvailable NOTIFY stateChanged)
    // Imported slide decks (batch 10).
    Q_PROPERTY(QObject *decks READ decks CONSTANT)
    Q_PROPERTY(int selectedDeck READ selectedDeck NOTIFY stateChanged)
    Q_PROPERTY(QString deckSummary READ deckSummary NOTIFY stateChanged)
    Q_PROPERTY(QString deckDetails READ deckDetails NOTIFY stateChanged)
    Q_PROPERTY(bool decksAvailable READ decksAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool decksBusy READ decksBusy NOTIFY stateChanged)
    // Plans, .lumen files and backups (batch 11).
    Q_PROPERTY(QObject *savedPlans READ savedPlans CONSTANT)
    Q_PROPERTY(QObject *backups READ backups CONSTANT)
    Q_PROPERTY(QString planMessage READ planMessage NOTIFY stateChanged)
    Q_PROPERTY(QString lastBackup READ lastBackup NOTIFY stateChanged)
    Q_PROPERTY(bool backupsAvailable READ backupsAvailable NOTIFY stateChanged)
    // Pictures for the Preview and Live frames when they show media.
    Q_PROPERTY(QUrl previewPoster READ previewPoster NOTIFY stateChanged)
    Q_PROPERTY(QUrl livePoster READ livePoster NOTIFY stateChanged)
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
    // How the Preview and Live frames draw their slides (batch 8 themes).
    Q_PROPERTY(QVariantMap previewStyle READ previewStyle NOTIFY stateChanged)
    Q_PROPERTY(QString previewFooter READ previewFooter NOTIFY stateChanged)
    Q_PROPERTY(QColor previewBackground READ previewBackground NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap liveStyle READ liveStyle NOTIFY stateChanged)
    Q_PROPERTY(QString liveFooter READ liveFooter NOTIFY stateChanged)
    Q_PROPERTY(QColor liveBackground READ liveBackground NOTIFY stateChanged)
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
    QObject *media() { return &m_mediaModel; }
    int selectedMedia() const { return m_selectedMedia; }
    QString mediaSummary() const { return m_mediaSummary; }
    bool mediaAvailable() const { return m_mediaLibrary != nullptr; }
    QObject *decks() { return &m_deckModel; }
    int selectedDeck() const { return m_selectedDeck; }
    QString deckSummary() const { return m_deckSummary; }
    QString deckDetails() const { return m_deckDetails; }
    bool decksAvailable() const { return m_decks != nullptr; }
    bool decksBusy() const { return m_decksBusy; }
    QObject *savedPlans() { return &m_savedPlansModel; }
    QObject *backups() { return &m_backupModel; }
    QString planMessage() const { return m_planMessage; }
    QString lastBackup() const;
    bool backupsAvailable() const { return m_backups != nullptr; }
    QString planId() const { return m_plan ? m_plan->id : QString(); }
    QUrl previewPoster() const;
    QUrl livePoster() const;

    // Turns on the Media tab (batch 9).
    void attachMedia(MediaLibrary &media);
    Q_INVOKABLE QString importMedia(const QList<QUrl> &urls);
    Q_INVOKABLE bool showMedia(int row);       // in the slide grid, ready to send live
    Q_INVOKABLE bool useAsBackground(int row); // a looping, silent motion background
    Q_INVOKABLE bool addMediaToPlan(int row);
    // Turns on the Presentations tab (batch 10).
    void attachPresentations(PresentationLibrary &decks, DeckImporter::Options options = {});
    // Draws the slides on another thread (an office conversion can take a
    // while); decksImported() follows. Returns false if an import is running.
    Q_INVOKABLE bool importPresentations(const QList<QUrl> &urls);
    Q_INVOKABLE bool showDeck(int row); // in the slide grid, ready to send live
    Q_INVOKABLE bool addDeckToPlan(int row);
    // Plans (batch 11). Each change is saved at once.
    Q_INVOKABLE void refreshSavedPlans();
    Q_INVOKABLE bool newPlan(const QString &name);
    Q_INVOKABLE bool openPlan(int row);       // in savedPlans
    bool openPlanById(const QString &planId);
    Q_INVOKABLE bool duplicatePlan();         // the open plan, dated today
    Q_INVOKABLE bool renamePlan(const QString &name);
    Q_INVOKABLE bool deletePlan(int row);     // in savedPlans
    Q_INVOKABLE bool addHeader(const QString &text); // after the selected item
    Q_INVOKABLE bool removeItem(int index);
    Q_INVOKABLE bool moveItem(int from, int to);
    Q_INVOKABLE bool setItemNotes(int index, const QString &notes);
    Q_INVOKABLE bool exportPlan(const QUrl &file);
    Q_INVOKABLE bool importPlan(const QUrl &file);

    // Backups: where they go, and where imported media is copied.
    void attachBackups(Backups &backups, const QString &libraryPath, const QString &importFolder);
    Q_INVOKABLE bool backUpNow();
    void refreshBackups();
    // Records the restore and asks the app to restart (restartRequested).
    Q_INVOKABLE bool restoreBackup(int row);

    // Which plan and item are open, saved every 30 seconds and on each
    // change of plan (in "<library>.session.json"), so a restart after a
    // crash reopens them.
    void saveSession();
    // Reopens the saved plan and item; false if there is none.
    bool restoreSession();

    // The video Space would send next, if any (the renderer preloads it).
    QString previewVideo() const;
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
    QVariantMap previewStyle() const;
    QString previewFooter() const;
    QColor previewBackground() const;
    QVariantMap liveStyle() const;
    QString liveFooter() const;
    QColor liveBackground() const;
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
    // Re-applies themes to every item after a theme changed (what is on
    // screen now is left alone; the next slide sent uses the new look).
    void refreshThemes();
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
    void previewVideoChanged(const QString &path);
    void decksImported(const QString &summary);
    void restartRequested();

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

    void refreshMedia();
    void refreshDecks();
    void reloadPlan(int select);
    QList<QVariantMap> planRows() const;
    bool planFailed(const core::Status &status, const QString &what);

    QString m_planMessage;
    Backups *m_backups = nullptr;
    QString m_libraryPath;
    QString m_importFolder;
    QTimer m_sessionTimer;
    RowModel m_savedPlansModel{{"planId", "name", "date", "items", "open"}};
    RowModel m_backupModel{{"path", "made", "size"}};

    PresentationLibrary *m_decks = nullptr;
    DeckImporter::Options m_deckOptions;
    int m_selectedDeck = -1;
    QString m_deckSummary;
    QString m_deckDetails;
    bool m_decksBusy = false;
    void updatePreviewVideo();

    MediaLibrary *m_mediaLibrary = nullptr;
    int m_selectedMedia = -1;
    QString m_mediaSummary;
    QString m_previewVideo;
    // The operator chose a background (e.g. a motion loop); themes leave it.
    bool m_manualBackground = false;

    RowModel m_planModel{{"kind", "title", "subtitle", "notes", "header"}};
    RowModel m_mediaModel{{"title", "kind", "duration", "thumbnail", "presentationId", "path", "loop"}};
    RowModel m_deckModel{{"title", "slides", "thumbnail", "presentationId", "method"}};
    RowModel m_slideModel{{"group", "colour", "text", "thumbnail"}};
    RowModel m_songModel{{"title", "author", "copyright", "key", "presentationId"}};
};

} // namespace lumen::app
