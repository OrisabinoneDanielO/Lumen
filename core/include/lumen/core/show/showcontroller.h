#pragma once

#include "lumen/core/show/scene.h"

#include <QObject>

namespace lumen::core::show {

// Owns what is on air. Every change produces a new Scene with a higher
// sequence number and emits sceneChanged; changes that alter nothing emit
// nothing. The renderer animates transitions itself, so a fade that has
// started finishes even if the UI stalls.
class ShowController : public QObject
{
    Q_OBJECT

public:
    explicit ShowController(QObject *parent = nullptr);

    const Scene &scene() const { return m_scene; }

    void setContent(Layer layer, const Content &content);
    void clear(Layer layer);
    // Empties all six layers. Logo and Black are separate controls and stay
    // as they are.
    void clearAll();
    void setTransition(Layer layer, const Transition &transition);

    // Overrides above every layer. Turning one off shows the layers exactly as
    // they were, including anything changed while the override was up.
    void setLogo(bool on);
    void toggleLogo() { setLogo(!m_scene.logo); }
    void setBlack(bool on);
    void toggleBlack() { setBlack(!m_scene.black); }
    void setLogoContent(const Content &content);
    void setOverrideTransition(const Transition &transition);

    // Takes over a scene that is already on air, e.g. from a renderer that
    // kept running while the UI restarted. Later changes continue its
    // sequence numbers. Emits nothing: the renderer already shows it.
    void adopt(const Scene &scene);

signals:
    void sceneChanged(const lumen::core::show::Scene &scene);

private:
    void commit(const Scene &next);

    Scene m_scene;
};

} // namespace lumen::core::show
