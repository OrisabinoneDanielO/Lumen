#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpServer;
class QTcpSocket;

namespace lumen::app {

class LiveControl;

// The phone and tablet remote (batch 19): a small web server on the local
// network. Its page (remote.html, built in) shows what is live and next and
// the plan, with buttons for next, previous, Clear, Logo and Black; it works
// in any phone browser, with no app and no internet.
//
// Off until the operator turns it on; then it shows a six-digit code that
// the phone must give once (five wrong codes from a device: a minute's wait).
//
// HTTP API (JSON), for the page and for anything else on the network:
//   GET  /                   the remote page
//   GET  /api/state          what is live, next and in the plan
//   POST /api/<command>      a LiveControl command; JSON body: its arguments
// The code goes in an "X-Lumen-Code" header (or "?code=").
class RemoteServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)
    Q_PROPERTY(QStringList addresses READ addresses NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    RemoteServer(LiveControl &control, quint16 port, QObject *parent = nullptr);
    ~RemoteServer() override;

    bool enabled() const { return m_server != nullptr; }
    void setEnabled(bool on);
    QString code() const { return m_code; }
    quint16 port() const;
    // Where phones open the remote: "http://192.168.1.20:8180/".
    QStringList addresses() const;
    QString status() const { return m_status; }

signals:
    void changed();

private:
    void serve(QTcpSocket *socket);
    QByteArray respond(const QByteArray &method, const QByteArray &target, const QHash<QByteArray, QByteArray> &headers,
                       const QByteArray &body, const QString &caller);

    LiveControl &m_control;
    quint16 m_port;
    QTcpServer *m_server = nullptr;
    QString m_code;
    QString m_status;
    QElapsedTimer m_clock;
    QHash<QString, std::pair<int, qint64>> m_wrongCodes;
};

} // namespace lumen::app
