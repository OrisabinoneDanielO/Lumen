#pragma once

#include "lumen/core/render/protocol.h"
#include "lumen/core/result.h"

#include <QColor>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStringList>

namespace lumen::core::render {

// The operator UI's connection to the render process.
//
// start() connects to a renderer that is already running (for example one
// left up after the UI crashed) or launches lumen-render as a detached process
// so that it outlives the UI. Calls block for at most their timeout.
class RenderLink
{
public:
    struct Options
    {
        QString serverName = defaultServerName();
        QString executable;     // empty: findExecutable()
        QStringList arguments;  // extra arguments for lumen-render
        int startTimeoutMs = 10000;
    };

    explicit RenderLink(Options options);
    ~RenderLink();
    RenderLink(const RenderLink &) = delete;
    RenderLink &operator=(const RenderLink &) = delete;

    // Connects to a running renderer only.
    Status connectToRunning(int timeoutMs);

    // Connects to a running renderer, or launches one and connects.
    Status start();

    bool isConnected() const;

    // Sends one message and waits for its reply. A reply with "ok": false is
    // returned as an Error.
    Result<QJsonObject> request(const QJsonObject &message, int timeoutMs = 3000);

    Status show(const QColor &background, const QString &text);
    Status quit();

    // lumen-render next to the running executable (also inside a macOS bundle).
    static QString findExecutable();

private:
    Options m_options;
    QLocalSocket m_socket;
    QByteArray m_buffer;
};

} // namespace lumen::core::render
