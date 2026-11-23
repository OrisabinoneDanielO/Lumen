#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>

class QLocalSocket;

namespace lumen::render {

class OutputWindow;

// Accepts commands from the operator UI (see lumen/core/render/protocol.h).
// Any number of clients may connect. A client that disconnects without
// "quit" changes nothing on screen.
class RenderServer : public QObject
{
    Q_OBJECT

public:
    // testMode enables "grab", which writes the current frame to a file.
    RenderServer(OutputWindow &output, bool testMode, QObject *parent = nullptr);
    ~RenderServer() override;

    // Fails if another renderer already owns the name.
    bool listen(const QString &name, QString *error);
    QString serverName() const { return m_server.serverName(); }

    // Handles one request and returns the reply (without the version field).
    QJsonObject handle(const QJsonObject &request);

signals:
    void quitRequested();

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);

    OutputWindow &m_output;
    bool m_testMode;
    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_buffers;
};

} // namespace lumen::render
