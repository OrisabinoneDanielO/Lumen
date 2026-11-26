#pragma once

// Loads the real operator window (Lumen.Ui Main.qml) with an OperatorController
// over an in-memory library holding the mockup's sample service.

#include "operatorcontroller.h"
#include "servicedata.h"

#include "lumen/core/data/database.h"
#include "lumen/core/show/showcontroller.h"

#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <memory>

namespace lumen::test {

class OperatorHarness
{
public:
    // Returns false (with a message) if the window could not be loaded.
    bool load(bool editAvailable, QString *error)
    {
        auto db = core::data::Database::open(core::data::Database::inMemory());
        if (!db.ok() || !app::seedSampleService(*db.value()).ok()) {
            *error = QStringLiteral("could not create the sample library");
            return false;
        }
        m_db = std::move(db).value();
        controller = std::make_unique<app::OperatorController>(show, editAvailable);
        auto plan = app::loadLatestPlan(*m_db);
        if (!plan.ok()) {
            *error = plan.error().message;
            return false;
        }
        controller->setPlan(plan.value());

        engine = std::make_unique<QQmlApplicationEngine>();
        engine->setInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue<QObject *>(controller.get())}});
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

    // Clicks the middle of an item. Returns false if it does not exist.
    bool click(const QString &name) const
    {
        QQuickItem *target = item(name);
        if (!target)
            return false;
        const QPointF centre = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, {}, centre.toPoint());
        return true;
    }

    void key(int key) const { QTest::keyClick(window, Qt::Key(key)); }

    core::show::ShowController show;
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
