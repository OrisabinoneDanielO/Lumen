#pragma once

#include "lumen/core/render/protocol.h"
#include "lumen/core/result.h"

#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
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
    Status start();

    bool isConnected() const;

    // Sends without waiting for the reply.
    Status send(const QJsonObject &message);

    // Sends and waits for this message's reply. A reply with "ok": false is
    // returned as an Error.
    Result<QJsonObject> request(const QJsonObject &message, int timeoutMs = 3000);

    Status quit();

    // lumen-render next to the running executable (also inside a macOS bundle).
    static QString findExecutable();

signals:
    // A reply to send() reported a failure.
    void replyFailed(const QString &error);

private:
    void drain();

    Options m_options;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    quint64 m_nextId = 1;
    QSet<QString> m_waiting;           // ids request() is waiting for
    QHash<QString, QJsonObject> m_replies;
};

} // namespace lumen::core::render
