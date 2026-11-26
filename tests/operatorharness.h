#pragma once

// Loads the real operator window (Lumen.Ui Main.qml) with an OperatorController
// over an in-memory library holding the mockup's sample service, or over a
// library the test provides. The song library (search, import), a media
// library (thumbnails in a temporary folder) and a presentation library
// (slide pictures in a temporary folder; drawn by Lumen, without office
// programs, so results do not depend on what is installed) are attached.

#include "medialibrary.h"
#include "operatorcontroller.h"
#include "presentationlibrary.h"
#include "scripturecontroller.h"
#include "sampleservice.h"
#include "screensetup.h"
#include "updatechecker.h"
#include "servicedata.h"
#include "themecontroller.h"
#include "themes.h"
#include "editorcontroller.h"
#include "messagetemplates.h"
#include "sharing.h"
#include "cuecontroller.h"
#include "livecontrol.h"
#include "midiinput.h"
#include "oscserver.h"
#include "remoteserver.h"
#include "propscontroller.h"
#include "stagecontroller.h"
#include "timercontroller.h"

#include "lumen/core/data/database.h"
#include "lumen/core/show/showcontroller.h"
#include "lumen/core/songs/songlibrary.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQuickTest/quicktest.h>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(Lumen_SlidesPlugin)

#include <memory>

namespace lumen::test {

class OperatorHarness
{
public:
    ~OperatorHarness()
    {
        engine.reset();
        updates.reset();
        screens.reset();
        cues.reset();
        midi.reset();
        osc.reset();
        remote.reset();
        live.reset();
        sharing.reset();
        editor.reset();
        templates.reset();
        props.reset();
        stage.reset();
        timers.reset();
        themes.reset();
        scripture.reset();
        controller.reset();
        decks.reset();
        media.reset();
        songs.reset();
        m_db.reset();
    }

    // Returns false (with a message) if the window could not be loaded.
    bool load(bool editAvailable, QString *error,
              std::unique_ptr<core::data::Database> library = nullptr)
    {
        if (library) {
            m_db = std::move(library);
        } else {
            auto db = core::data::Database::open(core::data::Database::inMemory());
            if (!db.ok() || !app::seedSampleService(*db.value()).ok()) {
                *error = QStringLiteral("could not create the sample library");
                return false;
            }
            m_db = std::move(db).value();
        }
        if (!app::ensureDefaultThemes(*m_db).ok()) {
            *error = QStringLiteral("could not set up the default themes");
            return false;
        }
        songs = std::make_unique<core::songs::SongLibrary>(*m_db);
        if (!songs->indexMissing().ok()) {
            *error = QStringLiteral("could not index the songs");
            return false;
        }
        controller = std::make_unique<app::OperatorController>(show, editAvailable);
        auto plan = app::loadLatestPlan(*m_db);
        if (!plan.ok()) {
            *error = plan.error().message;
            return false;
        }
        controller->setPlan(plan.value());
        controller->attachLibrary(*m_db, *songs);
        scripture = std::make_unique<app::ScriptureController>(*controller);
        scripture->setStores(bibles, nullptr);
        themes = std::make_unique<app::ThemeController>(*controller);
        themes->attach(*m_db);
        timers = std::make_unique<app::TimerController>(*controller, show);
        timers->attach(*m_db);
        stage = std::make_unique<app::StageController>(*controller, *timers);
        props = std::make_unique<app::PropsController>(show);
        props->attach(*m_db);
        templates = std::make_unique<app::MessageTemplates>(*controller);
        templates->attach(*m_db);
        editor = std::make_unique<app::EditorController>(*controller);
        editor->attach(*m_db, songs.get());
        sharing = std::make_unique<app::Sharing>(*m_db, QStringLiteral("Test PC"), thumbnails.path());
        live = std::make_unique<app::LiveControl>(*controller);
        remote = std::make_unique<app::RemoteServer>(*live, 0);
        osc = std::make_unique<app::OscServer>(*live, 0);
        midi = std::make_unique<app::MidiInput>(*live);
        cues = std::make_unique<app::CueController>(*controller);
        screens = std::make_unique<app::ScreenSetup>();
        updates = std::make_unique<app::UpdateChecker>(updateOptions);
        cues->attach(*m_db);
        media = std::make_unique<app::MediaLibrary>(*m_db, thumbnails.path());
        controller->attachMedia(*media);
        decks = std::make_unique<app::PresentationLibrary>(*m_db, slidePictures.path());
        app::DeckImporter::Options deckOptions;
        deckOptions.office = app::DeckImporter::Office::Never;
        controller->attachPresentations(*decks, deckOptions);

        engine = std::make_unique<QQmlApplicationEngine>();
        engine->setInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue<QObject *>(controller.get())},
             {QStringLiteral("scripture"), QVariant::fromValue<QObject *>(scripture.get())},
             {QStringLiteral("themes"), QVariant::fromValue<QObject *>(themes.get())},
             {QStringLiteral("timerController"), QVariant::fromValue<QObject *>(timers.get())},
             {QStringLiteral("stageController"), QVariant::fromValue<QObject *>(stage.get())},
             {QStringLiteral("propsController"), QVariant::fromValue<QObject *>(props.get())},
             {QStringLiteral("messageTemplates"), QVariant::fromValue<QObject *>(templates.get())},
             {QStringLiteral("editorController"), QVariant::fromValue<QObject *>(editor.get())},
             {QStringLiteral("sharing"), QVariant::fromValue<QObject *>(sharing.get())},
             {QStringLiteral("remote"), QVariant::fromValue<QObject *>(remote.get())},
             {QStringLiteral("osc"), QVariant::fromValue<QObject *>(osc.get())},
             {QStringLiteral("midi"), QVariant::fromValue<QObject *>(midi.get())},
             {QStringLiteral("cues"), QVariant::fromValue<QObject *>(cues.get())},
             {QStringLiteral("screenSetup"), QVariant::fromValue<QObject *>(screens.get())},
             {QStringLiteral("updates"), QVariant::fromValue<QObject *>(updates.get())}});
        engine->loadFromModule("Lumen.Ui", "Main");
        if (engine->rootObjects().isEmpty()) {
            *error = QStringLiteral("Main.qml did not load");
            return false;
        }
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().constFirst());
        window->requestActivate();
        if (!QTest::qWaitForWindowExposed(window) || !QTest::qWaitForWindowActive(window)) {
            *error = QStringLiteral("the window did not appear");
            return false;
        }
        return true;
    }

    // Searches the visual item tree (which also reaches Repeater delegates).
    QQuickItem *item(const QString &name) const { return find(window->contentItem(), name); }

    // Waits until Qt Quick has laid items out. Items created for a new model
    // all start at the same spot until the next polish, so a click before
    // then would land on the wrong one (a person cannot click that fast).
    void settle() const
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QQuickTest::qWaitForPolish(window, 2000);
    }

    // Clicks the middle of an item. Returns false if it does not exist.
    bool click(const QString &name) const
    {
        settle();
        QQuickItem *target = item(name);
        if (!target)
            return false;
        const QPointF centre = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
        return true;
    }

    void key(int key) const { QTest::keyClick(window, Qt::Key(key)); }

    // Clicks the library row showing a song title, once the list has settled
    // (rows from a previous search are deleted later, so an old row can still
    // carry the same name for a moment).
    bool clickSongRow(const QString &title) const
    {
        QDeadlineTimer deadline(3000);
        while (!deadline.hasExpired()) {
            settle();
            for (int row = 0;; ++row) {
                QQuickItem *candidate = item(QStringLiteral("songRow-%1").arg(row));
                if (!candidate)
                    break;
                const QVariantList cells = candidate->property("cells").toList();
                if (candidate->isVisible() && !cells.isEmpty() && cells.front().toString() == title) {
                    const QPointF centre = candidate->mapToScene(
                        QPointF(candidate->width() / 2, candidate->height() / 2));
                    QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
                    return true;
                }
            }
            QTest::qWait(20);
        }
        return false;
    }

    core::data::Database &database() { return *m_db; }

    // Set before load() to give the scripture panel Bibles.
    core::scripture::BibleStore *bibles = nullptr;

    core::show::ShowController show;
    std::unique_ptr<core::songs::SongLibrary> songs;
    QTemporaryDir thumbnails;
    std::unique_ptr<app::MediaLibrary> media;
    QTemporaryDir slidePictures;
    std::unique_ptr<app::PresentationLibrary> decks;
    std::unique_ptr<app::ScriptureController> scripture;
    std::unique_ptr<app::ThemeController> themes;
    std::unique_ptr<app::TimerController> timers;
    std::unique_ptr<app::StageController> stage;
    std::unique_ptr<app::PropsController> props;
    std::unique_ptr<app::MessageTemplates> templates;
    std::unique_ptr<app::EditorController> editor;
    std::unique_ptr<app::Sharing> sharing;
    std::unique_ptr<app::LiveControl> live;
    std::unique_ptr<app::RemoteServer> remote;
    std::unique_ptr<app::OscServer> osc;
    std::unique_ptr<app::MidiInput> midi;
    std::unique_ptr<app::ScreenSetup> screens;
    // Where update checks go (tests point it at a local server before load()).
    app::UpdateChecker::Options updateOptions;
    std::unique_ptr<app::UpdateChecker> updates;
    std::unique_ptr<app::CueController> cues;
    std::unique_ptr<app::OperatorController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;

private:
    static QQuickItem *find(QQuickItem *item, const QString &name)
    {
        if (!item)
            return nullptr;
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = find(child, name))
                return found;
        }
        return nullptr;
    }

    std::unique_ptr<core::data::Database> m_db;
};

} // namespace lumen::test
