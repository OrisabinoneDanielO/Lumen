#pragma once

// Loads the real operator window (Lumen.Ui Main.qml) with an OperatorController
// over an in-memory library holding the mockup's sample service, or over a
// library the test provides. The song library (search, import) is attached.

#include "operatorcontroller.h"
#include "scripturecontroller.h"
#include "servicedata.h"

#include "lumen/core/data/database.h"
#include "lumen/core/show/showcontroller.h"
#include "lumen/core/songs/songlibrary.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QtQuickTest/quicktest.h>

#include <memory>

namespace lumen::test {

class OperatorHarness
{
public:
    ~OperatorHarness()
    {
        engine.reset();
        scripture.reset();
        controller.reset();
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

        engine = std::make_unique<QQmlApplicationEngine>();
        engine->setInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue<QObject *>(controller.get())},
             {QStringLiteral("scripture"), QVariant::fromValue<QObject *>(scripture.get())}});
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
    std::unique_ptr<app::ScriptureController> scripture;
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
