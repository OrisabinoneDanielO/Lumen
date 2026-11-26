#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantList>

class QUdpSocket;

namespace lumen::app {

class LiveControl;

// OSC control (batch 19): OSC messages over UDP on the local network, from
// show-control software, a lighting desk or a tablet app (TouchOSC and the
// like). Off until turned on. Addresses (numbers are 1-based, as people
// count):
//   /lumen/next  /lumen/previous  /lumen/item/next  /lumen/item/previous
//   /lumen/item <n>   /lumen/slide <n>   /lumen/section <n>   /lumen/group <label>
//   /lumen/clear/background|slide|props|messages   /lumen/clearall
//   /lumen/logo   /lumen/black   /lumen/message <text>   /lumen/message/hide
// A button that sends 1 when pressed and 0 when let go acts on the press.
// Bundles are read too.
class OscServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    OscServer(LiveControl &control, quint16 port, QObject *parent = nullptr);

    bool enabled() const { return m_socket != nullptr; }
    void setEnabled(bool on);
    int port() const { return m_port; }
    QString status() const { return m_status; }

    struct Message
    {
        QString address;
        QVariantList arguments; // int, float, string, bool
    };
    // The messages in an OSC packet (a message or a bundle); none if it is
    // not OSC.
    static QList<Message> parse(const QByteArray &packet);
    // An OSC message as bytes (for tests and replies).
    static QByteArray encode(const QString &address, const QVariantList &arguments = {});

    // Runs one message; false if it was not a Lumen address.
    bool handle(const Message &message);

signals:
    void changed();

private:
    void read();

    LiveControl &m_control;
    quint16 m_port;
    QUdpSocket *m_socket = nullptr;
    QString m_status;
};

} // namespace lumen::app
