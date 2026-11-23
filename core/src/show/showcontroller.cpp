#include "lumen/core/show/showcontroller.h"

namespace lumen::core::show {

ShowController::ShowController(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<lumen::core::show::Scene>();
}

void ShowController::commit(const Scene &next)
{
    Scene candidate = next;
    candidate.sequence = m_scene.sequence;
    if (candidate == m_scene)
        return;
    candidate.sequence = m_scene.sequence + 1;
    m_scene = candidate;
    emit sceneChanged(m_scene);
}

void ShowController::setContent(Layer layer, const Content &content)
{
    Scene next = m_scene;
    next.layer(layer).content = content;
    commit(next);
}

void ShowController::clear(Layer layer)
{
    setContent(layer, Content::empty());
}

void ShowController::clearAll()
{
    Scene next = m_scene;
    for (LayerState &state : next.layers)
        state.content = Content::empty();
    commit(next);
}

void ShowController::setTransition(Layer layer, const Transition &transition)
{
    Scene next = m_scene;
    next.layer(layer).transition = transition;
    commit(next);
}

void ShowController::setLogo(bool on)
{
    Scene next = m_scene;
    next.logo = on;
    commit(next);
}

void ShowController::setBlack(bool on)
{
    Scene next = m_scene;
    next.black = on;
    commit(next);
}

void ShowController::setLogoContent(const Content &content)
{
    Scene next = m_scene;
    next.logoContent = content;
    commit(next);
}

void ShowController::setOverrideTransition(const Transition &transition)
{
    Scene next = m_scene;
    next.overrideTransition = transition;
    commit(next);
}

void ShowController::adopt(const Scene &scene)
{
    m_scene = scene;
}

} // namespace lumen::core::show
