#include "remoteserver.h"

#include "livecontrol.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

namespace lumen::app {

namespace {

constexpr qsizetype maxRequest = 64 * 1024;
constexpr int maxWrongCodes = 5;

QByteArray reply(int status, const QByteArray &type, const QByteArray &body)
{
    static const QHash<int, QByteArray> reasons{{200, "OK"}, {400, "Bad Request"}, {401, "Unauthorized"},
                                                {404, "Not Found"}, {405, "Method Not Allowed"},
                                                {429, "Too Many Requests"}};
    return "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasons.value(status, "Error") + "\r\n"
           "Content-Type: " + type + "\r\n"
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
           "Cache-Control: no-store\r\n"
           "Connection: close\r\n\r\n" + body;
}

QByteArray json(int status, const QJsonObject &object)
{
    return reply(status, "application/json; charset=utf-8", QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

RemoteServer::RemoteServer(LiveControl &control, quint16 port, QObject *parent)
    : QObject(parent), m_control(control), m_port(port)
{
    m_clock.start();
}

RemoteServer::~RemoteServer()
{
    setEnabled(false);
}

quint16 RemoteServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QStringList RemoteServer::addresses() const
{
    QStringList out;
    if (!m_server)
        return out;
    for (const QHostAddress &a : QNetworkInterface::allAddresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal())
            out << QStringLiteral("http://%1:%2/").arg(a.toString()).arg(port());
    }
    if (out.isEmpty())
        out << QStringLiteral("http://127.0.0.1:%1/").arg(port());
    return out;
}

void RemoteServer::setEnabled(bool on)
{
    if (on == enabled())
        return;
    if (!on) {
        delete m_server;
        m_server = nullptr;
        m_code.clear();
        m_status = tr("The remote is off");
        emit changed();
        return;
    }
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::Any, m_port) && !m_server->listen(QHostAddress::Any, 0)) {
        m_status = tr("The remote could not start: %1").arg(m_server->errorString());
        delete m_server;
        m_server = nullptr;
        emit changed();
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server->nextPendingConnection())
            serve(socket);
    });
    m_code = QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(1000000), 6, 10, QLatin1Char('0'));
    m_status = tr("Open the address on a phone on this network and enter the code");
    emit changed();
}

void RemoteServer::serve(QTcpSocket *socket)
{
    auto buffer = std::make_shared<QByteArray>();
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    QTimer::singleShot(10000, socket, [socket] { socket->abort(); });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
        buffer->append(socket->readAll());
        if (buffer->size() > maxRequest) {
            socket->write(reply(400, "text/plain", "Too large"));
            socket->disconnectFromHost();
            return;
        }
        const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
        QHash<QByteArray, QByteArray> headers;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines[i].indexOf(':');
            if (colon > 0)
                headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
        const qsizetype length = headers.value("content-length").toLongLong();
        if (buffer->size() < headerEnd + 4 + length)
            return; // the body is still coming
        const QByteArray body = buffer->mid(headerEnd + 4, length);
        socket->write(respond(requestLine.value(0), requestLine.value(1), headers, body, socket->peerAddress().toString()));
        socket->disconnectFromHost();
        buffer->clear();
        buffer->append(QByteArray(maxRequest + 1, '\0')); // anything more is ignored
    });
}

QByteArray RemoteServer::respond(const QByteArray &method, const QByteArray &target,
                                 const QHash<QByteArray, QByteArray> &headers, const QByteArray &body, const QString &caller)
{
    const QUrl url(QString::fromUtf8(target));
    const QString path = url.path();
    if (method == "GET" && (path == QLatin1String("/") || path == QLatin1String("/index.html"))) {
        QFile page(QStringLiteral(":/lumen/remote/remote.html"));
        if (!page.open(QIODevice::ReadOnly))
            return reply(404, "text/plain", "The remote page is missing");
        return reply(200, "text/html; charset=utf-8", page.readAll());
    }
    if (!path.startsWith(QLatin1String("/api/")))
        return reply(404, "text/plain", "Not found");

    // The code, with a limit on wrong guesses.
    auto &[wrongCount, since] = m_wrongCodes[caller];
    if (wrongCount >= maxWrongCodes && m_clock.elapsed() - since < 60000)
        return json(429, {{"ok", false}, {"error", "too many wrong codes; wait a minute"}});
    QString code = QString::fromUtf8(headers.value("x-lumen-code"));
    if (code.isEmpty())
        code = QUrlQuery(url).queryItemValue(QStringLiteral("code"));
    if (m_code.isEmpty() || code != m_code) {
        if (wrongCount >= maxWrongCodes)
            wrongCount = 0;
        ++wrongCount;
        since = m_clock.elapsed();
        return json(401, {{"ok", false}, {"error", "wrong code"}});
    }
    wrongCount = 0;

    const QString command = path.mid(5);
    if (command == QLatin1String("state")) {
        if (method != "GET")
            return json(405, {{"ok", false}, {"error", "use GET"}});
        return json(200, {{"ok", true}, {"state", m_control.state()}});
    }
    if (method != "POST")
        return json(405, {{"ok", false}, {"error", "use POST"}});
    if (!LiveControl::commands().contains(command))
        return json(404, {{"ok", false}, {"error", "unknown command"}});
    QString error;
    const bool done = m_control.run(command, QJsonDocument::fromJson(body).object(), &error);
    if (done)
        emit m_control.ran(command, tr("remote"));
    return json(200, {{"ok", done}, {"error", error}, {"state", m_control.state()}});
}

} // namespace lumen::app
