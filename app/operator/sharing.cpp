#include "sharing.h"

#include "lumenfile.h"
#include "planstore.h"

#include "lumen/core/data/repository.h"

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkInterface>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QUuid>

namespace lumen::app {

using namespace core;
using namespace core::data;

namespace {

constexpr qint64 maxHeader = 64 * 1024;
constexpr qint64 maxPayload = qint64(8) * 1024 * 1024 * 1024; // 8 GB: a plan with long videos
constexpr int maxWrongCodes = 5;                              // per address, then a minute's wait

QByteArray line(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace

Sharing::Sharing(Database &db, QString name, QString importFolder, Options options, QObject *parent)
    : QObject(parent), m_db(db), m_name(std::move(name)), m_importFolder(std::move(importFolder)), m_options(options),
      m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_clock.start();
    m_announceTimer.setInterval(2000);
    connect(&m_announceTimer, &QTimer::timeout, this, [this] {
        announce();
        refreshPeers();
    });
}

Sharing::~Sharing()
{
    setEnabled(false);
}

quint16 Sharing::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

QString Sharing::address() const
{
    if (!m_server)
        return {};
    for (const QHostAddress &a : QNetworkInterface::allAddresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal())
            return QStringLiteral("%1:%2").arg(a.toString()).arg(port());
    }
    return QStringLiteral("127.0.0.1:%1").arg(port());
}

void Sharing::setEnabled(bool on)
{
    if (on == enabled())
        return;
    if (!on) {
        delete m_server;
        m_server = nullptr;
        m_code.clear();
        if (!m_discovering) {
            m_announceTimer.stop();
            delete m_udp;
            m_udp = nullptr;
            m_peers.clear();
            refreshPeers();
        }
        m_status = tr("Sharing is off");
        emit changed();
        return;
    }
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::Any, m_options.port)) {
        m_status = tr("Sharing could not start: %1").arg(m_server->errorString());
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

    // Finding each other: announcements on a local multicast group.
    m_status.clear();
    listen();
    m_announceTimer.start();
    announce();
    if (m_status.isEmpty() || m_status == tr("Sharing is off"))
        m_status = tr("Sharing as \"%1\"").arg(m_name);
    emit changed();
}

void Sharing::discover()
{
    m_discovering = true;
    listen();
    m_announceTimer.start(); // also expires computers that went away
}

void Sharing::listen()
{
    if (m_udp)
        return;
    m_udp = new QUdpSocket(this);
    if (!m_udp->bind(QHostAddress::AnyIPv4, m_options.discoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        m_status = tr("Other computers cannot be found automatically here; type their address");
        return;
    }
    if (!m_udp->joinMulticastGroup(m_options.group))
        m_status = tr("Other computers cannot be found automatically here; type their address");
    m_udp->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    connect(m_udp, &QUdpSocket::readyRead, this, &Sharing::readAnnouncements);
}

void Sharing::announce()
{
    if (!m_udp || !m_server)
        return;
    const QByteArray datagram = QJsonDocument(QJsonObject{{"lumen", 1}, {"id", m_id}, {"name", m_name},
                                                          {"port", int(port())}}).toJson(QJsonDocument::Compact);
    m_udp->writeDatagram(datagram, m_options.group, m_options.discoveryPort);
}

void Sharing::readAnnouncements()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        QHostAddress from;
        QByteArray datagram(int(std::min<qint64>(m_udp->pendingDatagramSize(), 4096)), Qt::Uninitialized);
        m_udp->readDatagram(datagram.data(), datagram.size(), &from);
        const QJsonObject json = QJsonDocument::fromJson(datagram).object();
        const QString id = json.value("id").toString();
        if (json.value("lumen").toInt() != 1 || id.isEmpty() || id == m_id)
            continue;
        Peer &peer = m_peers[id];
        peer = {id, json.value("name").toString().left(80), QHostAddress(from.toIPv4Address()),
                quint16(json.value("port").toInt()), m_clock.elapsed()};
    }
    refreshPeers();
}

void Sharing::refreshPeers()
{
    // Gone quiet for ten seconds: gone.
    for (auto it = m_peers.begin(); it != m_peers.end();)
        it = m_clock.elapsed() - it->seenMs > 10000 ? m_peers.erase(it) : std::next(it);
    QList<QVariantMap> rows;
    for (const Peer &p : m_peers)
        rows << QVariantMap{{"name", p.name}, {"address", QStringLiteral("%1:%2").arg(p.address.toString()).arg(p.port)}, {"id", p.id}};
    std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value("name").toString().localeAwareCompare(b.value("name").toString()) < 0;
    });
    m_peerModel.updateRows(rows);
}

// ---- serving -----------------------------------------------------------------

void Sharing::serve(QTcpSocket *socket)
{
    // Read the request line (and a sent file), answer, close.
    auto buffer = std::make_shared<QByteArray>();
    auto header = std::make_shared<QJsonObject>();
    auto expected = std::make_shared<qint64>(-1);
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    QTimer::singleShot(m_options.timeoutMs * 8, socket, [socket] { socket->abort(); });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, header, expected] {
        buffer->append(socket->readAll());
        if (*expected < 0) {
            const qsizetype end = buffer->indexOf('\n');
            if (end < 0) {
                if (buffer->size() > maxHeader)
                    socket->abort();
                return;
            }
            *header = QJsonDocument::fromJson(buffer->left(end)).object();
            buffer->remove(0, end + 1);
            const QString cmd = header->value("cmd").toString();
            *expected = cmd == QLatin1String("send") || cmd == QLatin1String("screenFile") ? header->value("size").toInteger() : 0;
            if (*expected < 0 || *expected > maxPayload) {
                socket->write(line({{"ok", false}, {"error", "too large"}}));
                socket->disconnectFromHost();
                return;
            }
        }
        if (buffer->size() < *expected)
            return;
        QByteArray reply;
        const QJsonObject answer = handle(socket->peerAddress().toString(), *header, *buffer, &reply);
        socket->write(line(answer));
        if (!reply.isEmpty())
            socket->write(reply);
        socket->disconnectFromHost();
        *expected = std::numeric_limits<qint64>::max(); // done; ignore anything else
    });
}

QJsonObject Sharing::handle(const QString &caller, const QJsonObject &request, const QByteArray &payload,
                            QByteArray *replyPayload)
{
    if (request.value("lumen").toInt() != 1)
        return {{"ok", false}, {"error", "not Lumen"}};
    // Five wrong codes from one computer: it waits a minute before trying
    // again (six digits cannot be guessed like that).
    auto &[wrongCount, since] = m_wrongCodes[caller];
    if (wrongCount >= maxWrongCodes && m_clock.elapsed() - since < 60000)
        return {{"ok", false}, {"error", "too many wrong codes; wait a minute"}};
    if (m_code.isEmpty() || request.value("code").toString() != m_code) {
        if (wrongCount >= maxWrongCodes)
            wrongCount = 0;
        ++wrongCount;
        since = m_clock.elapsed();
        return {{"ok", false}, {"error", "wrong code"}};
    }
    wrongCount = 0;

    const QString cmd = request.value("cmd").toString();
    if (cmd.startsWith(QLatin1String("screen"))) {
        if (!m_screenHandler || !m_takesScreen)
            return {{"ok", false}, {"error", "this computer does not take a screen from others"}};
        return m_screenHandler(cmd, request, payload);
    }
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("shared.lumen"));
    if (cmd == QLatin1String("list")) {
        QJsonArray planRows;
        if (auto all = plans::list(m_db); all.ok()) {
            for (const plans::PlanSummary &p : all.value())
                planRows.append(QJsonObject{{"planId", p.id}, {"name", p.name},
                                            {"date", p.date.isValid() ? p.date.toString(Qt::ISODate) : QString()},
                                            {"items", p.items}});
        }
        QJsonArray songRows;
        if (auto all = Repository<Presentation>(m_db).list(); all.ok()) {
            for (const Presentation &p : all.value()) {
                if (p.kind == PresentationKind::Song)
                    songRows.append(QJsonObject{{"presentationId", p.id}, {"title", p.title},
                                                {"author", p.metadata.value("author").toString()}});
            }
        }
        return {{"ok", true}, {"name", m_name}, {"plans", planRows}, {"songs", songRows},
                {"screen", m_screenHandler && m_takesScreen}}; // takes another computer's live output
    }
    if (cmd == QLatin1String("plan") || cmd == QLatin1String("songs")) {
        QStringList ids;
        for (const QJsonValue &id : request.value("ids").toArray())
            ids << id.toString();
        const Status exported = cmd == QLatin1String("plan")
                                    ? lumenfile::exportPlan(m_db, request.value("planId").toString(), file)
                                    : lumenfile::exportPresentations(m_db, ids, file);
        QFile in(file);
        if (!exported.ok() || !in.open(QIODevice::ReadOnly))
            return {{"ok", false}, {"error", exported.ok() ? QStringLiteral("could not read the export") : exported.error().message}};
        *replyPayload = in.readAll();
        return {{"ok", true}, {"size", replyPayload->size()}};
    }
    if (cmd == QLatin1String("send")) {
        QString summary;
        if (!importBytes(payload, &summary))
            return {{"ok", false}, {"error", summary}};
        return {{"ok", true}, {"summary", summary}};
    }
    return {{"ok", false}, {"error", "unknown request"}};
}

bool Sharing::importBytes(const QByteArray &bytes, QString *summary)
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("received.lumen"));
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
        *summary = tr("could not store the file");
        return false;
    }
    out.close();
    auto report = lumenfile::importFile(m_db, file, m_importFolder);
    if (!report.ok()) {
        *summary = report.error().message;
        return false;
    }
    m_lastPlanId = report.value().planId;
    *summary = report.value().summary();
    emit imported();
    return true;
}

// ---- asking ------------------------------------------------------------------

Result<QJsonObject> Sharing::request(const QJsonObject &request, const QByteArray &payload, QByteArray *replyPayload)
{
    if (m_remoteHost.isNull() || m_remotePort == 0)
        return Error{Error::Code::Invalid, tr("Choose a computer first")};
    QTcpSocket socket;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QByteArray received;
    qint64 expected = -1;
    QJsonObject reply;
    bool finished = false;
    connect(&socket, &QTcpSocket::readyRead, &loop, [&] {
        received.append(socket.readAll());
        timeout.start(m_options.timeoutMs); // still coming: wait on
        if (expected < 0) {
            const qsizetype end = received.indexOf('\n');
            if (end < 0)
                return;
            reply = QJsonDocument::fromJson(received.left(end)).object();
            received.remove(0, end + 1);
            expected = reply.value("size").toInteger();
        }
        if (received.size() >= expected) {
            finished = true;
            loop.quit();
        }
    });
    connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    connect(&socket, &QTcpSocket::errorOccurred, &loop, [&](QAbstractSocket::SocketError error) {
        if (error != QAbstractSocket::RemoteHostClosedError)
            loop.quit();
    });
    connect(&socket, &QTcpSocket::connected, &loop, [&] {
        QJsonObject header = request;
        header.insert("lumen", 1);
        header.insert("code", m_remoteCode);
        if (!payload.isEmpty())
            header.insert("size", payload.size());
        socket.write(line(header));
        socket.write(payload);
    });
    m_busy = true;
    emit changed();
    socket.connectToHost(m_remoteHost, m_remotePort);
    timeout.start(m_options.timeoutMs);
    loop.exec();
    m_busy = false;
    emit changed();
    if (!finished && expected >= 0 && received.size() >= expected)
        finished = true;
    if (!finished)
        return Error{Error::Code::Unavailable, socket.error() == QAbstractSocket::UnknownSocketError
                                                   ? tr("The other computer did not answer")
                                                   : tr("The other computer could not be reached: %1").arg(socket.errorString())};
    if (!reply.value("ok").toBool()) {
        const QString error = reply.value("error").toString();
        return Error{Error::Code::Invalid, error == QLatin1String("wrong code") ? tr("That is not the other computer's code")
                                                                               : tr("The other computer said: %1").arg(error)};
    }
    if (replyPayload)
        *replyPayload = received.left(expected);
    return reply;
}

bool Sharing::fail(const QString &message)
{
    m_status = message;
    emit changed();
    return false;
}

bool Sharing::connectTo(int peerRow, const QString &code)
{
    const QString address = m_peerModel.get(peerRow).value("address").toString();
    if (address.isEmpty())
        return fail(tr("Choose a computer"));
    return connectToAddress(address, code);
}

bool Sharing::connectToAddress(const QString &hostAndPort, const QString &code)
{
    const qsizetype colon = hostAndPort.lastIndexOf(u':');
    const QHostAddress host(colon > 0 ? hostAndPort.left(colon).trimmed() : hostAndPort.trimmed());
    const int port = colon > 0 ? hostAndPort.mid(colon + 1).toInt() : 0;
    if (host.isNull() || port <= 0 || port > 65535)
        return fail(tr("Give the address as shown on the other computer, e.g. 192.168.1.20:45460"));
    m_remoteHost = host;
    m_remotePort = quint16(port);
    m_remoteCode = code.trimmed();
    auto reply = request({{"cmd", "list"}});
    if (!reply.ok()) {
        m_planModel.setRows({});
        m_songModel.setRows({});
        return fail(reply.error().message);
    }
    QList<QVariantMap> plansRows;
    for (const QJsonValue &v : reply.value().value("plans").toArray()) {
        QVariantMap row = v.toObject().toVariantMap();
        const QDate date = QDate::fromString(row.value("date").toString(), Qt::ISODate);
        row.insert("date", date.isValid() ? QLocale().toString(date, QLocale::ShortFormat) : QString());
        plansRows << row;
    }
    QList<QVariantMap> songRows;
    for (const QJsonValue &v : reply.value().value("songs").toArray())
        songRows << v.toObject().toVariantMap();
    m_planModel.setRows(plansRows);
    m_songModel.setRows(songRows);
    m_remoteName = reply.value().value("name").toString();
    m_remoteTakesScreen = reply.value().value("screen").toBool();
    m_status = tr("Connected to \"%1\": %2 plans, %3 songs")
                   .arg(reply.value().value("name").toString())
                   .arg(plansRows.size())
                   .arg(songRows.size());
    emit changed();
    return true;
}

bool Sharing::copyPlan(int remotePlanRow)
{
    const QString planId = m_planModel.get(remotePlanRow).value("planId").toString();
    if (planId.isEmpty())
        return fail(tr("Choose a plan"));
    QByteArray bytes;
    auto reply = request({{"cmd", "plan"}, {"planId", planId}}, {}, &bytes);
    if (!reply.ok())
        return fail(reply.error().message);
    QString summary;
    if (!importBytes(bytes, &summary))
        return fail(tr("Not copied: %1").arg(summary));
    m_status = summary;
    emit changed();
    return true;
}

bool Sharing::copySongs(const QVariantList &remoteSongRows)
{
    QJsonArray ids;
    for (const QVariant &row : remoteSongRows)
        ids.append(m_songModel.get(row.toInt()).value("presentationId").toString());
    if (ids.isEmpty())
        return fail(tr("Choose songs"));
    QByteArray bytes;
    auto reply = request({{"cmd", "songs"}, {"ids", ids}}, {}, &bytes);
    if (!reply.ok())
        return fail(reply.error().message);
    QString summary;
    if (!importBytes(bytes, &summary))
        return fail(tr("Not copied: %1").arg(summary));
    m_status = summary;
    emit changed();
    return true;
}

bool Sharing::sendPlan(const QString &planId)
{
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("send.lumen"));
    if (auto exported = lumenfile::exportPlan(m_db, planId, file); !exported.ok())
        return fail(tr("Not sent: %1").arg(exported.error().message));
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return fail(tr("Not sent: the plan could not be read"));
    auto reply = request({{"cmd", "send"}}, in.readAll());
    if (!reply.ok())
        return fail(tr("Not sent: %1").arg(reply.error().message));
    m_status = tr("Sent. The other computer: %1").arg(reply.value().value("summary").toString());
    emit changed();
    return true;
}

} // namespace lumen::app
