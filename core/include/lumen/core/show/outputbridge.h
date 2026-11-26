#pragma once

#include "lumen/core/result.h"
#include "lumen/core/show/scene.h"

#include <QObject>

namespace lumen::core::render {
class RenderLink;
}

namespace lumen::core::show {

class ShowController;

// Keeps a renderer showing the show controller's scene.
//
// attach() first asks the renderer what it is showing and has the controller
// adopt it, so a UI restarted after a crash carries on from what is on air
// instead of blanking the screen. After that, every scene change is sent
// without waiting.
class OutputBridge : public QObject
{
    Q_OBJECT

public:
    OutputBridge(ShowController &controller, render::RenderLink &link, QObject *parent = nullptr);

    // Returns the scene that was on air when attaching.
    Result<Scene> attach();

signals:
    void sendFailed(const QString &error);

private:
    void push(const Scene &scene);

    ShowController &m_controller;
    render::RenderLink &m_link;
};

} // namespace lumen::core::show
