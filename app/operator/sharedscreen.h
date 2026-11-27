#pragma once

#include "sharing.h"

#include "lumen/core/show/scene.h"
#include "lumen/core/show/showcontroller.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>

namespace lumen::app {

// Two operators, one screen: a second Lumen computer (a laptop running
// scripture, say) puts what it sends live on the screen of the computer
// driving the projector. That computer holds the truth; both see it in
// their Live column, and its phone remote and keys still work.
//
// It travels over the sharing connection (sharing.h: the same six-digit
// code), one JSON request per connection:
//   "screen"      {"scene" (this computer's, files as references),
//                  "changed": [layer names, "logo", "black"]} takes those
//                 layers and overrides; the reply has the screen's whole
//                 "scene", or the "missing" files to send first
//   "screenState" the reply has the screen's "scene" (and "videoPosition")
//   "screenFile"  {"ref"} + the file's bytes, once per file
//   "screenVideo" {"action": "pause" | "play" | "seek", "positionMs"}
// Files are named by a reference ("lumen-file:<key>"); the screen keeps
// each in its own folder, so a video or picture travels once.

// Walks the local files a content uses (videos, pictures, posters, editor
// elements, corner pictures), letting `map` change each path.
void rewriteFiles(core::show::Content &content, const std::function<QString(const QString &path)> &map);

// The computer driving the projector: takes changes from the other
// computer when allowed (Share panel).
class ScreenHost : public QObject
{
    Q_OBJECT

public:
    ScreenHost(core::show::ShowController &show, QString folder, QObject *parent = nullptr);

    // Sharing::ScreenHandler. `extraState` adds to "screenState" replies
    // (the live video's position).
    QJsonObject handle(const QString &cmd, const QJsonObject &request, const QByteArray &payload);
    void setExtraState(std::function<QJsonObject()> extra) { m_extra = std::move(extra); }

signals:
    void videoControlRequested(const QString &action, qint64 positionMs);
    void changedByOther(); // the other computer changed the screen

private:
    core::show::ShowController &m_show;
    QString m_folder;
    std::function<QJsonObject()> m_extra;
};

// The other computer: sends what goes live here to that screen, and
// follows what is on it.
class ScreenLink : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString target READ target NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    explicit ScreenLink(core::show::ShowController &show, QObject *parent = nullptr);

    bool active() const { return m_active; }
    QString target() const { return m_target; }
    QString status() const { return m_status; }

    // Starts sending to the computer Sharing is connected to (it must take
    // a screen: Sharing::remoteTakesScreen).
    Q_INVOKABLE bool start(lumen::app::Sharing *sharing);
    bool start(const QHostAddress &host, quint16 port, const QString &code, const QString &name);
    Q_INVOKABLE void stop();
    // Pause, play or seek the video on that screen.
    void videoControl(const QString &action, qint64 positionMs);
    // How often it asks the screen what it shows (ms); tests go faster.
    void setPollInterval(int ms) { m_poll.setInterval(ms); }

signals:
    void changed();
    // The screen's live video, as OperatorController::setOutputStatus
    // takes it ({"videoPosition": ...}, with this computer's paths).
    void remoteStatus(const QJsonObject &status);

private:
    using Callback = std::function<void(const QJsonObject &reply, const QString &error)>;
    void request(const QJsonObject &request, const QByteArray &payload, Callback done);
    void sceneChanged();
    void send();
    void poll();
    void upload(QStringList refs);
    void adoptHost(const QJsonObject &sceneJson);
    QString refFor(const QString &path);
    core::show::Content toRefs(core::show::Content content);
    core::show::Scene toLocal(core::show::Scene scene) const;
    void fail(const QString &message);

    core::show::ShowController &m_show;
    bool m_active = false;
    bool m_busy = false;     // a "screen" request is out
    bool m_dirty = false;    // changed here since the last one
    bool m_applying = false; // taking on the screen's scene
    QString m_target;
    QString m_status;
    QHostAddress m_host;
    quint16 m_port = 0;
    QString m_code;
    core::show::Scene m_lastHost; // what the screen showed last (this computer's paths)
    QHash<QString, QString> m_refs; // file key -> local path
    QTimer m_poll;
};

} // namespace lumen::app
