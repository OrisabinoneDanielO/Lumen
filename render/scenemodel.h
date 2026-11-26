#pragma once

#include "cuerunner.h"
#include "lumen/core/show/scene.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace lumen::render {

using core::show::Content;
using core::show::Scene;
using core::show::Transition;

// One piece of content, as QML sees it.
class ContentModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QColor colour READ colour NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QUrl image READ image NOTIFY changed)
    // True if it hides everything beneath it (an opaque colour fill).
    Q_PROPERTY(bool opaque READ opaque NOTIFY changed)
    Q_PROPERTY(QVariantMap style READ style NOTIFY changed)
    Q_PROPERTY(QString footer READ footer NOTIFY changed)
    Q_PROPERTY(QUrl video READ video NOTIFY changed)
    Q_PROPERTY(bool loop READ loop NOTIFY changed)
    Q_PROPERTY(double volume READ volume NOTIFY changed)
    Q_PROPERTY(QVariantMap timer READ timer NOTIFY changed)
    Q_PROPERTY(QVariantMap prop READ prop NOTIFY changed)
    Q_PROPERTY(QVariantList elements READ elements NOTIFY changed)

public:
    using QObject::QObject;

    void set(const Content &content);
    const Content &content() const { return m_content; }

    QString kind() const;
    QColor colour() const { return m_content.colour; }
    QString text() const { return m_content.text; }
    QUrl image() const;
    bool opaque() const;
    QVariantMap style() const { return m_content.style.toVariantMap(); }
    QString footer() const { return m_content.footer; }
    QUrl video() const;
    bool loop() const { return m_content.loop; }
    double volume() const { return m_content.volume; }
    QVariantMap timer() const { return m_content.timer.toVariantMap(); }
    QVariantMap prop() const { return m_content.prop.toVariantMap(); }
    QVariantList elements() const { return m_content.elements.toVariantList(); }

signals:
    void changed();

private:
    Content m_content;
};

// One layer: what it showed before, what it shows now, and how far the
// transition between them has got (1 = finished).
class LayerModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int index READ index CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QObject *previous READ previous CONSTANT)
    Q_PROPERTY(QObject *current READ current CONSTANT)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

public:
    LayerModel(int index, QObject *parent = nullptr);

    int index() const { return m_index; }
    QString name() const;
    QObject *previous() { return &m_previous; }
    QObject *current() { return &m_current; }
    double progress() const { return m_progress; }

    // Starts a transition to content (no-op if it is already the content).
    void show(const Content &content, const Transition &transition, qint64 nowMs);
    void update(qint64 nowMs);

signals:
    void progressChanged();

private:
    void setProgress(double progress);

    int m_index;
    ContentModel m_previous;
    ContentModel m_current;
    qint64 m_startMs = 0;
    int m_durationMs = 0;
    double m_progress = 1.0;
};

// A level that moves linearly from one value to a target over a duration.
class Fader
{
public:
    double level() const { return m_level; }
    // Returns true if the level changed.
    bool setTarget(double target, int durationMs, qint64 nowMs);
    bool update(qint64 nowMs);

private:
    double m_level = 0.0;
    double m_from = 0.0;
    double m_target = 0.0;
    qint64 m_startMs = 0;
    int m_durationMs = 0;
};

// The whole output: six layers plus the Logo and Black overrides.
class SceneModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<QObject *> layers READ layers CONSTANT)
    Q_PROPERTY(QObject *logo READ logo CONSTANT)
    Q_PROPERTY(double logoLevel READ logoLevel NOTIFY levelsChanged)
    Q_PROPERTY(double blackLevel READ blackLevel NOTIFY levelsChanged)
    // Videos opened ahead of use (the item in Preview), newest last.
    Q_PROPERTY(QList<QUrl> preloads READ preloads NOTIFY preloadsChanged)
    // Wall-clock time for timers (ms since 1970 UTC), from this process's own
    // clock; changes every 100 ms.
    Q_PROPERTY(qint64 wallMs READ wallMs NOTIFY wallChanged)
    // The stage display's state (stage windows, batch 13).
    Q_PROPERTY(QVariantMap stage READ stage NOTIFY stageChanged)
    // While the operator sets up the screens: a large label naming this
    // screen ({number, name, role}), drawn above everything; empty otherwise.
    Q_PROPERTY(QVariantMap identify READ identify NOTIFY identifyChanged)

public:
    explicit SceneModel(QObject *parent = nullptr);

    QList<QObject *> layers() const;
    QObject *logo() { return &m_logo; }
    double logoLevel() const { return m_logoFader.level(); }
    double blackLevel() const { return m_blackFader.level(); }

    const Scene &scene() const { return m_scene; }
    void apply(const Scene &scene, qint64 nowMs);
    void update(qint64 nowMs);

    QList<QUrl> preloads() const { return m_preloads; }
    void preload(const QString &path);

    qint64 wallMs() const { return m_wallShownMs; }
    // Tests: makes the wall clock read wallMs at clock time nowMs.
    void setWallClock(qint64 wallMs, qint64 nowMs);
    Q_INVOKABLE QString timerText(const QVariantMap &timer, qint64 wallMs) const;

    // The render clock, for cue runners (set by the output window).
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }
    // Cued content (batch 20): where its timeline is, for status.
    QJsonObject timelineStatus(qint64 nowMs) const;
    // MIDI bytes for the timelines that follow timecode (tests feed them
    // here; MtcInput feeds each runner itself).
    void feedMidi(const QByteArray &bytes, qint64 nowMs);

    QVariantMap identify() const { return m_identify.toVariantMap(); }
    void setIdentify(const QJsonObject &identify)
    {
        if (identify == m_identify)
            return;
        m_identify = identify;
        emit identifyChanged();
    }

    QVariantMap stage() const { return m_stage.toVariantMap(); }
    void setStage(const QJsonObject &stage)
    {
        if (stage == m_stage)
            return;
        m_stage = stage;
        emit stageChanged();
    }

signals:
    void identifyChanged();
    void levelsChanged();
    void preloadsChanged();
    void wallChanged();
    void stageChanged();

private:
    Scene m_scene;
    std::vector<std::unique_ptr<LayerModel>> m_layers;
    ContentModel m_logo;
    Fader m_logoFader;
    Fader m_blackFader;
    QList<QUrl> m_preloads;
    qint64 m_wallBaseMs = -1; // wall clock minus render clock
    qint64 m_wallShownMs = 0;
    QJsonObject m_identify;
    QJsonObject m_stage;
    std::array<std::unique_ptr<CueRunner>, core::show::layerCount> m_cueRunners;
    std::function<qint64()> m_clock;
};

} // namespace lumen::render
