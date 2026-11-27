#pragma once

#include "keymap.h"
#include "medialibrary.h"
#include "presentationlibrary.h"
#include "backups.h"
#include "rowmodel.h"
#include "servicedata.h"
#include "usagelog.h"

#include <memory>

#include "lumen/core/show/showcontroller.h"
#include "lumen/core/songs/songlibrary.h"

#include <QObject>
#include <QUrl>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <optional>

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
    Q_PROPERTY(QString planId READ planId NOTIFY stateChanged)
    Q_PROPERTY(QString lastBackup READ lastBackup NOTIFY stateChanged)
    Q_PROPERTY(bool backupsAvailable READ backupsAvailable NOTIFY stateChanged)
    // Timers on Preview and Live (countdown slides), updated as they tick.
    Q_PROPERTY(QString previewTimerText READ previewTimerText NOTIFY timersTicked)
    Q_PROPERTY(QString liveTimerText READ liveTimerText NOTIFY timersTicked)
    // Slides made in the editor (batch 16), for the Preview and Live frames.
    Q_PROPERTY(QVariantList previewElements READ previewElements NOTIFY stateChanged)
    Q_PROPERTY(QVariantList liveElements READ liveElements NOTIFY stateChanged)
    // Messages (batch 12): the banner on the Messages layer.
    Q_PROPERTY(QString messageShown READ messageShown NOTIFY stateChanged)
    Q_PROPERTY(QStringList recentMessages READ recentMessages NOTIFY stateChanged)
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
    Q_PROPERTY(bool shownIsVideo READ shownIsVideo NOTIFY stateChanged)
    Q_PROPERTY(QString selectedPresentationId READ selectedPresentationId NOTIFY stateChanged)
    // How slides and full-screen media change: "cut", "fade", "push",
    // "pushUp", "wipe", "zoom", over transitionMs (remembered).
    Q_PROPERTY(QString transition READ transition WRITE setTransition NOTIFY transitionChanged)
    Q_PROPERTY(int transitionMs READ transitionMs WRITE setTransitionMs NOTIFY transitionChanged)
    Q_PROPERTY(QVariantList transitions READ transitions NOTIFY stateChanged) // {key, name}
    Q_PROPERTY(bool shownVideoLoops READ shownVideoLoops WRITE setShownVideoLoops NOTIFY stateChanged)
    // The shown video's start and end points ("m:ss"; end empty: the end).
    // A song: its key as the band plays it, and how far it is transposed.
    Q_PROPERTY(bool shownIsSong READ shownIsSong NOTIFY stateChanged)
    Q_PROPERTY(QString shownKey READ shownKey NOTIFY stateChanged)
    Q_PROPERTY(int shownTranspose READ shownTranspose NOTIFY stateChanged)
    Q_PROPERTY(QString shownVideoStart READ shownVideoStart NOTIFY stateChanged)
    Q_PROPERTY(QString shownVideoEnd READ shownVideoEnd NOTIFY stateChanged)
    // The video on the screen now (the Media layer), as its output reports.
    Q_PROPERTY(bool liveVideo READ liveVideo NOTIFY stateChanged)
    Q_PROPERTY(double liveVideoPositionMs READ liveVideoPositionMs NOTIFY videoPositionChanged)
    Q_PROPERTY(double liveVideoDurationMs READ liveVideoDurationMs NOTIFY videoPositionChanged)
    Q_PROPERTY(bool liveVideoPlaying READ liveVideoPlaying NOTIFY videoPositionChanged)
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
    // A video on Live, or the next one in Preview, that the output could not
    // play: what is wrong, for the operator (empty when all is well).
    Q_PROPERTY(QString liveProblem READ liveProblem NOTIFY stateChanged)
    Q_PROPERTY(QString previewProblem READ previewProblem NOTIFY stateChanged)
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
    // The output's "status" reply (polled): its video errors.
    void setOutputStatus(const QJsonObject &status);
    QString liveProblem() const;
    QString previewProblem() const;
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
    QString previewTimerText() const;
    QString liveTimerText() const;
    QVariantList previewElements() const;
    QVariantList liveElements() const;
    QJsonArray resolveTimers(const QJsonArray &elements) const;
    // The editor (batch 16): what it opens, and reloading after it saves.
    QString selectedPresentationId() const { return selected() ? selected()->presentationId : QString(); }
    void refreshAfterEdit();
    // Something arrived in the library (sharing, batch 18): plans, songs,
    // media and decks are listed again.
    void refreshLibrary();
    // What a slide of an item looks like on the Slide layer (cues, batch 20).
    core::show::Content slideContent(const ItemView &view, int index) const;
    const ItemView *selectedItem() const { return selected(); }
    // Plays the selected item's cues with its backing track on the output
    // (batch 20), or stops it.
    bool playCues(const QJsonObject &timeline, qint64 offsetMs = 0);
    void stopCues();
    bool cuesPlaying() const;
    QString messageShown() const;
    QStringList recentMessages() const { return m_recentMessages; }

    // Where countdown slides get their timer (set by the TimerController).
    using TimerSource = std::function<std::optional<QJsonObject>(const QString &name)>;
    void setTimerSource(TimerSource source) { m_timerSource = std::move(source); }

    // What the stage display shows (batch 13): the item and slide on air, the
    // slide Space sends next, the arrangement with the current part marked,
    // and the item's key and notes. See protocol.h for the fields.
    QJsonObject stageState() const;

    // Messages: a line of text in the banner, without touching the slide.
    // ticker: scroll across the banner (batch 14) instead of standing still.
    Q_INVOKABLE bool showMessage(const QString &text, bool ticker = false);
    Q_INVOKABLE void hideMessage();
    // The Main output light in the toolbar turns the output on and off.
    Q_INVOKABLE void toggleOutput() { emit outputToggleRequested(!m_outputConnected); }
    QUrl previewPoster() const;
    QUrl livePoster() const;

    // Turns on the Media tab (batch 9).
    void attachMedia(MediaLibrary &media);
    Q_INVOKABLE QString importMedia(const QList<QUrl> &urls);
    Q_INVOKABLE bool showMedia(int row);       // in the slide grid, ready to send live
    // A video or picture from the library double-clicked: live at once (a
    // video starts playing); in Edit mode only shown, as everything there.
    Q_INVOKABLE bool playMedia(int row);
    // The output opens a library video ahead of a click (pointed at).
    Q_INVOKABLE void preloadMedia(int row);
    // A plan item double-clicked: its first slide live at once (a click only
    // shows it, in the slide grid and Preview).
    Q_INVOKABLE bool playItem(int index);
    // The item shown is a video: whether it loops until cleared or plays
    // once and stops (kept with the video in the library).
    bool shownIsVideo() const;
    bool shownVideoLoops() const;
    bool shownIsSong() const;
    QString shownKey() const;
    int shownTranspose() const;
    // Moves the shown song's chords (and key) up or down by semitones, for
    // the stage display; kept with the song. Within an octave either way.
    Q_INVOKABLE void transposeShown(int semitones);
    // ChordPro lines as styled text for the stage: chords in colour before
    // their syllable, transposed.
    static QString chordsForStage(const QString &chordPro, int transpose, const QString &key);
    QString shownVideoStart() const;
    QString shownVideoEnd() const;
    // Sets the shown video's start and end ("1:05", "65", "1:05.5"; an empty
    // end: play to the end). False (with a plan message) if unreadable.
    Q_INVOKABLE bool setShownVideoTrim(const QString &start, const QString &end);
    bool liveVideo() const;
    double liveVideoPositionMs() const { return liveVideo() ? double(m_livePositionMs) : 0.0; }
    double liveVideoDurationMs() const { return liveVideo() ? double(m_liveDurationMs) : 0.0; }
    bool liveVideoPlaying() const { return liveVideo() && m_livePlaying; }
    Q_INVOKABLE void toggleVideoPlaying();
    Q_INVOKABLE void seekVideo(double positionMs);
    // "1:05" from 65000; parses "1:05", "65" or "1:05.5" (-1: unreadable).
    static QString formatTime(qint64 ms);
    static qint64 parseTime(const QString &text);
    Q_INVOKABLE void setShownVideoLoops(bool loops);

    QString transition() const { return m_transition; }
    void setTransition(const QString &kind);
    int transitionMs() const { return m_transitionMs; }
    void setTransitionMs(int ms);
    QVariantList transitions() const;
    // A library song double-clicked: its first slide live at once.
    Q_INVOKABLE bool playSong(int row);
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
    // The language changed: words made here are made again.
    void refreshTexts();
    Q_INVOKABLE bool newPlan(const QString &name);
    Q_INVOKABLE bool openPlan(int row);       // in savedPlans
    bool openPlanById(const QString &planId);
    Q_INVOKABLE bool duplicatePlan();         // the open plan, dated today
    Q_INVOKABLE bool renamePlan(const QString &name);
    Q_INVOKABLE bool deletePlan(int row);     // in savedPlans
    Q_INVOKABLE bool deleteCurrentPlan();     // the open plan (its songs stay in the library)
    Q_INVOKABLE bool addHeader(const QString &text); // after the selected item
    Q_INVOKABLE bool removeItem(int index);
    Q_INVOKABLE bool moveItem(int from, int to);
    // Dropped on the plan: a library song ("song"), video or picture
    // ("media") or presentation ("deck"), put in at position `at` (the end
    // if out of range).
    Q_INVOKABLE bool addToPlanAt(const QString &source, int row, int at);
    Q_INVOKABLE bool setItemNotes(int index, const QString &notes);
    Q_INVOKABLE bool exportPlan(const QUrl &file);
    Q_INVOKABLE bool importPlan(const QUrl &file);

    // Backups: where they go, and where imported media is copied.
    void attachBackups(Backups &backups, const QString &libraryPath, const QString &importFolder);
    Q_INVOKABLE bool backUpNow();
    void refreshBackups();
    // Records the restore and asks the app to restart (restartRequested).
    Q_INVOKABLE bool restoreBackup(int row);
    // What the open library holds now ("12 items · 2 plans"), to compare
    // with a backup before restoring it.
    Q_INVOKABLE QString currentLibraryContents() const;
    static QString libraryContents(const QString &path);

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
    // CCLI reporting: the songs shown live in the last `months` months (0:
    // all), as a CSV file. Says how many in the plan's message line.
    Q_INVOKABLE bool exportSongUsage(const QUrl &file, int months);
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
    // Page Down / Page Up: the next or previous plan item with slides
    // (headers are skipped). From a library song, scripture or deck: back to
    // the plan, after (or before) the item that was live.
    Q_INVOKABLE void nextItem() { stepItem(1); }
    Q_INVOKABLE void previousItem() { stepItem(-1); }
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
    void transitionChanged();
    void stateChanged();
    void scriptureRequested(); // Ctrl+B
    void previewVideoChanged(const QString &path);
    void videoPositionChanged();
    // For the outputs: pause, play or seek the video on the Media layer.
    void videoControlRequested(const QString &action, qint64 positionMs);
    void decksImported(const QString &summary);
    void slideSentLive(int index); // a slide of the selected item (cue recording, batch 20)
    void restartRequested();
    void timersTicked();
    void outputToggleRequested(bool on);

private:
    void stepItem(int direction);
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
    std::unique_ptr<UsageLog> m_usage; // songs shown live (CCLI reporting)
    QString m_search;
    int m_selectedSong = -1;
    QString m_importSummary;
    QString m_importDetails;
    std::optional<ItemView> m_librarySong;
    QString m_transition = QStringLiteral("cut");
    QHash<QString, QString> m_videoErrors; // local path -> why the output could not play it
    QString m_livePath; // the video the last status was about
    qint64 m_livePositionMs = 0;
    qint64 m_liveDurationMs = 0;
    bool m_livePlaying = false;
    QString problemFor(const QString &path) const;
    int m_transitionMs = 600;
    void applyTransition();

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
    TimerSource m_timerSource;
    QTimer m_timerTick;
    QStringList m_recentMessages;
    Backups *m_backups = nullptr;
    QString m_libraryPath;
    QString m_importFolder;
    QTimer m_sessionTimer;
    RowModel m_savedPlansModel{{"planId", "name", "date", "items", "open"}};
    RowModel m_backupModel{{"path", "made", "size", "contents"}};

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
