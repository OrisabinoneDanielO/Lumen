#include "oscserver.h"

#include "livecontrol.h"

#include <QJsonObject>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QtEndian>

#include <cstring>

namespace lumen::app {

namespace {

// OSC strings end with a nul and are padded to a multiple of four bytes.
bool readString(const QByteArray &data, qsizetype &at, QString *out)
{
    const qsizetype end = data.indexOf('\0', at);
    if (end < 0)
        return false;
    *out = QString::fromUtf8(data.mid(at, end - at));
    at = (end + 4) & ~qsizetype(3);
    return at <= data.size();
}

void appendString(QByteArray &out, const QByteArray &text)
{
    out += text;
    out += '\0';
    while (out.size() % 4)
        out += '\0';
}

void parseInto(const QByteArray &packet, QList<OscServer::Message> &out, int depth)
{
    if (depth > 4 || packet.size() < 4)
        return;
    if (packet.startsWith("#bundle")) {
        qsizetype at = 16; // "#bundle\0" and the time tag
        while (at + 4 <= packet.size()) {
            const qint32 size = qFromBigEndian<qint32>(packet.constData() + at);
            at += 4;
            if (size <= 0 || at + size > packet.size())
                return;
            parseInto(packet.mid(at, size), out, depth + 1);
            at += size;
        }
        return;
    }
    OscServer::Message message;
    qsizetype at = 0;
    QString tags;
    if (!readString(packet, at, &message.address) || !message.address.startsWith(u'/'))
        return;
    if (at < packet.size() && !readString(packet, at, &tags))
        return;
    for (const QChar tag : tags.mid(1)) {
        switch (tag.unicode()) {
        case 'i':
            if (at + 4 > packet.size())
                return;
            message.arguments << qFromBigEndian<qint32>(packet.constData() + at);
            at += 4;
            break;
        case 'f': {
            if (at + 4 > packet.size())
                return;
            const quint32 bits = qFromBigEndian<quint32>(packet.constData() + at);
            float value;
            std::memcpy(&value, &bits, 4);
            message.arguments << double(value);
            at += 4;
            break;
        }
        case 's': {
            QString text;
            if (!readString(packet, at, &text))
                return;
            message.arguments << text;
            break;
        }
        case 'T': message.arguments << true; break;
        case 'F': message.arguments << false; break;
        default: return; // a type Lumen does not read: ignore the message
        }
    }
    out << message;
}

} // namespace

OscServer::OscServer(LiveControl &control, quint16 port, QObject *parent) : QObject(parent), m_control(control), m_port(port) {}

void OscServer::setEnabled(bool on)
{
    if (on == enabled())
        return;
    if (!on) {
        delete m_socket;
        m_socket = nullptr;
        m_status = tr("OSC is off");
        emit changed();
        return;
    }
    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::Any, m_port)) {
        m_status = tr("OSC could not listen on port %1: %2").arg(m_port).arg(m_socket->errorString());
        delete m_socket;
        m_socket = nullptr;
        emit changed();
        return;
    }
    m_port = m_socket->localPort();
    connect(m_socket, &QUdpSocket::readyRead, this, &OscServer::read);
    m_status = tr("Listening for OSC on UDP port %1").arg(m_port);
    emit changed();
}

void OscServer::read()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram(65536);
        for (const Message &message : parse(datagram.data()))
            handle(message);
    }
}

QList<OscServer::Message> OscServer::parse(const QByteArray &packet)
{
    QList<Message> out;
    parseInto(packet, out, 0);
    return out;
}

QByteArray OscServer::encode(const QString &address, const QVariantList &arguments)
{
    QByteArray out;
    appendString(out, address.toUtf8());
    QByteArray tags(",");
    QByteArray data;
    for (const QVariant &argument : arguments) {
        if (argument.typeId() == QMetaType::Bool) {
            tags += argument.toBool() ? 'T' : 'F';
        } else if (argument.typeId() == QMetaType::Int) {
            tags += 'i';
            char bytes[4];
            qToBigEndian<qint32>(argument.toInt(), bytes);
            data.append(bytes, 4);
        } else if (argument.typeId() == QMetaType::Double || argument.typeId() == QMetaType::Float) {
            tags += 'f';
            const float value = argument.toFloat();
            quint32 bits;
            std::memcpy(&bits, &value, 4);
            char bytes[4];
            qToBigEndian<quint32>(bits, bytes);
            data.append(bytes, 4);
        } else {
            tags += 's';
            appendString(data, argument.toString().toUtf8());
        }
    }
    appendString(out, tags);
    return out + data;
}

bool OscServer::handle(const Message &message)
{
    const QStringList parts = message.address.toLower().split(u'/', Qt::SkipEmptyParts);
    if (parts.value(0) != QLatin1String("lumen") || parts.size() < 2)
        return false;
    // A button let go (0, or false) does nothing.
    const QVariant first = message.arguments.value(0);
    const bool release = message.arguments.size() == 1 && first.typeId() != QMetaType::QString && first.toDouble() == 0.0;
    const int number = first.toInt();
    const QString path = parts.mid(1).join(u'/');

    QString command;
    QJsonObject arguments;
    if (path == QLatin1String("next") || path == QLatin1String("previous") || path == QLatin1String("logo")
        || path == QLatin1String("black")) {
        command = path;
    } else if (path == QLatin1String("item/next")) {
        command = QStringLiteral("nextItem");
    } else if (path == QLatin1String("item/previous")) {
        command = QStringLiteral("previousItem");
    } else if (path == QLatin1String("clearall")) {
        command = QStringLiteral("clearAll");
    } else if (parts.value(1) == QLatin1String("clear") && parts.size() == 3) {
        command = QStringLiteral("clear");
        arguments = {{"layer", parts.value(2)}};
    } else if (path == QLatin1String("item") || path == QLatin1String("slide")) {
        command = path;
        arguments = {{"index", number - 1}};
    } else if (path == QLatin1String("section")) {
        command = path;
        arguments = {{"number", number}};
    } else if (path == QLatin1String("group")) {
        command = path;
        arguments = {{"label", first.toString()}};
    } else if (path == QLatin1String("message")) {
        command = QStringLiteral("message");
        arguments = {{"text", first.toString()}};
    } else if (path == QLatin1String("message/hide")) {
        command = QStringLiteral("hideMessage");
    } else {
        return false;
    }
    // Numbered commands take their number; the others act on a press.
    const bool numbered = command == QLatin1String("item") || command == QLatin1String("slide")
                          || command == QLatin1String("section");
    if (release && !numbered)
        return true;
    if (m_control.run(command, arguments))
        emit m_control.ran(command, tr("OSC"));
    return true;
}

} // namespace lumen::app
