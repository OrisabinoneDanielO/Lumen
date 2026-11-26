#pragma once

#include "rowmodel.h"

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;

namespace lumen::app {

// Sharing plans and songs with other Lumen computers on the local network
// (batch 18). No internet and no server: computers find each other with
// small multicast announcements on the local network and talk directly over
// TCP. Sharing is off until the operator turns it on; then this computer
// shows a six-digit code, and another computer must give that code to see
// or send anything (so nobody else on the church network can browse the
// library).
//
// What travels is a .lumen file (lumenfile.h): a plan with its songs, slides
// and media, or chosen songs. Importing reuses songs and media the library
// already has.
//
// Protocol (TCP, one request per connection): the client sends one JSON line
// {"lumen": 1, "code": "123456", "cmd": "list" | "plan" | "songs" | "send",
// ...}; for "send" the .lumen file's bytes follow ("size" of them). The
// server answers one JSON line {"ok": true | false, ...}; for "plan" and
// "songs" the file's bytes follow ("size").
// Announcements (UDP multicast 239.255.76.77): {"lumen": 1, "id", "name",
// "port"} every two seconds while sharing.
class Sharing : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QObject *peers READ peers CONSTANT)
    Q_PROPERTY(QObject *remotePlans READ remotePlans CONSTANT)
    Q_PROPERTY(QObject *remoteSongs READ remoteSongs CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
    struct Options
    {
        quint16 discoveryPort = 45459;
        QHostAddress group = QHostAddress(QStringLiteral("239.255.76.77"));
        quint16 port = 0; // TCP port to listen on (0: any free one)
        int timeoutMs = 15000;
    };

    Sharing(core::data::Database &db, QString name, QString importFolder, Options options, QObject *parent = nullptr);
    Sharing(core::data::Database &db, QString name, QString importFolder, QObject *parent = nullptr)
        : Sharing(db, std::move(name), std::move(importFolder), Options{}, parent) {}
    ~Sharing() override;

    bool enabled() const { return m_server != nullptr; }
    void setEnabled(bool on);
    QString code() const { return m_code; }
    QString name() const { return m_name; }
    quint16 port() const;
    QString address() const; // "192.168.1.20:45460", for typing in by hand
    QObject *peers() { return &m_peerModel; }
    QObject *remotePlans() { return &m_planModel; }
    QObject *remoteSongs() { return &m_songModel; }
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }

    // Looks for other Lumen computers (without sharing this one).
    Q_INVOKABLE void discover();

    // The other computer to talk to: a row of peers, or an address typed in
    // ("192.168.1.20:45460"); with its code.
    Q_INVOKABLE bool connectTo(int peerRow, const QString &code);
    Q_INVOKABLE bool connectToAddress(const QString &hostAndPort, const QString &code);

    // Copies from the connected computer into this library.
    Q_INVOKABLE bool copyPlan(int remotePlanRow);
    Q_INVOKABLE bool copySongs(const QVariantList &remoteSongRows);
    // Sends one of this library's plans to the connected computer.
    Q_INVOKABLE bool sendPlan(const QString &planId);

    // Results of the last transfer, for tests and the UI.
    QString lastPlanId() const { return m_lastPlanId; }

signals:
    void changed();
    void imported(); // something was added to this library (plans, songs, media)

private:
    struct Peer
    {
        QString id;
        QString name;
        QHostAddress address;
        quint16 port = 0;
        qint64 seenMs = 0;
    };

    void announce();
    void readAnnouncements();
    void refreshPeers();
    void serve(QTcpSocket *socket);
    QJsonObject handle(const QString &caller, const QJsonObject &request, const QByteArray &payload,
                       QByteArray *replyPayload);
    void listen();
    // One request to the connected computer; the reply and any file bytes.
    core::Result<QJsonObject> request(const QJsonObject &request, const QByteArray &payload = {},
                                      QByteArray *replyPayload = nullptr);
    bool fail(const QString &message);
    bool importBytes(const QByteArray &bytes, QString *summary);

    core::data::Database &m_db;
    QString m_name;
    QString m_importFolder;
    Options m_options;
    QString m_id;
    QString m_code;
    QTcpServer *m_server = nullptr;
    QUdpSocket *m_udp = nullptr;
    QTimer m_announceTimer;
    QElapsedTimer m_clock;
    QHash<QString, Peer> m_peers;
    RowModel m_peerModel{{"name", "address", "id"}};
    RowModel m_planModel{{"planId", "name", "date", "items"}};
    RowModel m_songModel{{"presentationId", "title", "author"}};
    QHostAddress m_remoteHost;
    quint16 m_remotePort = 0;
    QString m_remoteCode;
    QString m_status;
    bool m_busy = false;
    QString m_lastPlanId;
    bool m_discovering = false;
    QHash<QString, std::pair<int, qint64>> m_wrongCodes; // per calling address: count, when
};

} // namespace lumen::app
