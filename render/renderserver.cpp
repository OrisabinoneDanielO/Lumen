#include "renderserver.h"

#include "outputwindow.h"

#include "lumen/core/render/protocol.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QTimer>

namespace lumen::render {

using namespace lumen::core::render;

RenderServer::RenderServer(OutputWindow &output, bool testMode, QObject *parent)
    : QObject(parent), m_output(output), m_testMode(testMode)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &RenderServer::onNewConnection);
}

RenderServer::~RenderServer()
{
    // Client sockets are children of m_server and emit disconnected() while it
    // is destroyed, after m_buffers is gone. Detach them first.
    for (auto it = m_buffers.keyBegin(); it != m_buffers.keyEnd(); ++it)
        disconnect(*it, nullptr, this, nullptr);
    m_buffers.clear();
    m_server.close();
}

bool RenderServer::listen(const QString &name, QString *error)
{
    // A live renderer already answering on this name wins; a stale socket
    // file left by a killed renderer is removed.
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(300)) {
        probe.abort();
        if (error)
            *error = QStringLiteral("Another renderer is already running as %1").arg(name);
        return false;
    }
    QLocalServer::removeServer(name);
    if (!m_server.listen(name)) {
        if (error)
            *error = m_server.errorString();
        return false;
    }
    return true;
}

void RenderServer::onNewConnection()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        m_buffers.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            // Nothing on screen changes when a client goes away.
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void RenderServer::onReadyRead(QLocalSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();
    if (buffer.size() > maxLineBytes && !buffer.contains('\n')) {
        socket->abort();
        return;
    }

    qsizetype end;
    while ((end = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(end);
        buffer.remove(0, end + 1);
        if (line.trimmed().isEmpty())
            continue;

        const auto request = decode(line);
        const QJsonObject reply = request ? handle(*request)
                                          : errorReply(QStringLiteral("Request is not a JSON object"));
        socket->write(encode(reply));
        socket->flush();

        if (request && reply.value(QStringLiteral("ok")).toBool()
            && request->value(QStringLiteral("cmd")).toString() == QLatin1String("quit")) {
            socket->waitForBytesWritten(1000);
            QTimer::singleShot(0, this, &RenderServer::quitRequested);
            return;
        }
    }
}

QJsonObject RenderServer::handle(const QJsonObject &request)
{
    const int version = request.value(QStringLiteral("v")).toInt();
    if (version != protocolVersion) {
        return errorReply(QStringLiteral("Unsupported protocol version %1 (renderer speaks %2)")
                              .arg(version)
                              .arg(protocolVersion));
    }

    const QString command = request.value(QStringLiteral("cmd")).toString();

    if (command == QLatin1String("show")) {
        const QColor colour = QColor::fromString(request.value(QStringLiteral("colour")).toString());
        if (!colour.isValid())
            return errorReply(QStringLiteral("\"colour\" must be a colour such as #14213D"));
        m_output.setContent(colour, request.value(QStringLiteral("text")).toString());
        return okReply();
    }

    if (command == QLatin1String("status")) {
        return okReply({{QStringLiteral("frames"), qint64(m_output.framesPresented())},
                        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
                        {QStringLiteral("server"), m_server.serverName()}});
    }

    if (command == QLatin1String("grab")) {
        if (!m_testMode)
            return errorReply(QStringLiteral("\"grab\" is only available with --test-mode"));
        const QString path = request.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            return errorReply(QStringLiteral("\"grab\" needs a \"path\""));
        const QImage frame = m_output.grab();
        if (frame.isNull() || !frame.save(path))
            return errorReply(QStringLiteral("Could not save the frame to %1").arg(path));
        return okReply({{QStringLiteral("width"), frame.width()},
                        {QStringLiteral("height"), frame.height()}});
    }

    if (command == QLatin1String("quit"))
        return okReply();

    return errorReply(QStringLiteral("Unknown command \"%1\"").arg(command));
}

} // namespace lumen::render
