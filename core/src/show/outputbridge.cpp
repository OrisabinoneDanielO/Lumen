#include "lumen/core/show/outputbridge.h"

#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/showcontroller.h"

namespace lumen::core::show {

OutputBridge::OutputBridge(ShowController &controller, render::RenderLink &link, QObject *parent)
    : QObject(parent), m_controller(controller), m_link(link)
{
    connect(&m_link, &render::RenderLink::replyFailed, this, &OutputBridge::sendFailed);
}

Result<Scene> OutputBridge::attach()
{
    auto reply = m_link.request({{QStringLiteral("cmd"), QStringLiteral("getScene")}});
    if (!reply.ok())
        return reply.error();
    const auto onAir = sceneFromJson(reply.value().value(QStringLiteral("scene")).toObject());
    if (!onAir)
        return Error{Error::Code::Invalid, QStringLiteral("The renderer sent an unreadable scene")};

    m_controller.adopt(*onAir);
    connect(&m_controller, &ShowController::sceneChanged, this, &OutputBridge::push,
            Qt::UniqueConnection);
    return *onAir;
}

void OutputBridge::follow()
{
    connect(&m_controller, &ShowController::sceneChanged, this, &OutputBridge::push,
            Qt::UniqueConnection);
    push(m_controller.scene());
}

void OutputBridge::preload(const QString &videoPath)
{
    if (auto sent = m_link.send({{QStringLiteral("cmd"), QStringLiteral("preload")},
                                 {QStringLiteral("video"), videoPath}});
        !sent.ok())
        emit sendFailed(sent.error().message);
}

void OutputBridge::push(const Scene &scene)
{
    auto sent = m_link.send({{QStringLiteral("cmd"), QStringLiteral("scene")},
                             {QStringLiteral("scene"), toJson(scene)}});
    if (!sent.ok())
        emit sendFailed(sent.error().message);
}

} // namespace lumen::core::show
