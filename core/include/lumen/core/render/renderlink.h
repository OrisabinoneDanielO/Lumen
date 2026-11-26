#pragma once

#include "lumen/core/render/protocol.h"
#include "lumen/core/result.h"

#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>

#include <functional>
#include <QObject>
#include <QSet>
#include <QStringList>

namespace lumen::core::render {

// The operator UI's connection to the render process.
//
// start() connects to a renderer that is already running (for example one
// left up after the UI crashed) or launches lumen-render as a detached process
// so that it outlives the UI.
//
// send() never waits, so the UI never stalls on the renderer; failures arrive
// later through replyFailed(). request() waits for its reply, for start-up,
// status and tests.
class RenderLink : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString serverName = defaultServerName();
        QString executable;     // empty: findExecutable()
        QStringList arguments;  // extra arguments for lumen-render
        int startTimeoutMs = 10000;
    };

    explicit RenderLink(Options options, QObject *parent = nullptr);
    ~RenderLink() override;

    // Connects to a running renderer only.
    Status connectToRunning(int timeoutMs);

    // Connects to a running renderer, or launches one and connects.
    // A renderer already running from another version of Lumen (it outlives
    // the app, so an update can leave one running) cannot be talked to: it
    // is asked to quit, and this version's renderer starts in its place.
    Status start();

    bool isConnected() const;

    // Sends without waiting for the reply.
    Status send(const QJsonObject &message);

    // Sends and waits for this message's reply. A reply with "ok": false is
    // returned as an Error.
    Result<QJsonObject> request(const QJsonObject &message, int timeoutMs = 3000);

    Status quit();

    // Sends without waiting; onReply gets the reply when it comes (never if
    // the renderer has gone). For polling status without ever stalling the UI.
    Status ask(const QJsonObject &message, std::function<void(const QJsonObject &)> onReply);

    // lumen-render next to the running executable (also inside a macOS bundle).
    static QString findExecutable();

signals:
    // A reply to send() reported a failure.
    void replyFailed(const QString &error);

private:
    // Asks the connected renderer, which speaks another protocol version
    // (from its error), to quit, and waits until it has gone.
    void retire(const QString &versionError);

    void drain();

    Options m_options;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    quint64 m_nextId = 1;
    QSet<QString> m_waiting;           // ids request() is waiting for
    QHash<QString, QJsonObject> m_replies;
    QHash<QString, std::function<void(const QJsonObject &)>> m_callbacks; // ask()
};

} // namespace lumen::core::render
