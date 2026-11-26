#include "lumen/core/render/renderlink.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QLocalSocket>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

namespace lumen::core::render {

namespace {

Error linkError(const QString &message)
{
    return Error{Error::Code::Unavailable, message};
}

} // namespace

RenderLink::RenderLink(Options options, QObject *parent)
    : QObject(parent), m_options(std::move(options))
{
    connect(&m_socket, &QLocalSocket::readyRead, this, &RenderLink::drain);
}

RenderLink::~RenderLink()
{
    // Disconnecting without "quit" leaves the output up, which is what a crash
    // does too. A normal exit calls quit() first.
    disconnect(&m_socket, nullptr, this, nullptr);
    m_socket.abort();
}

bool RenderLink::isConnected() const
{
    return m_socket.state() == QLocalSocket::ConnectedState;
}

QString RenderLink::findExecutable()
{
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("lumen-render.exe");
#else
    const QString name = QStringLiteral("lumen-render");
#endif
    const QDir appDir(QCoreApplication::applicationDirPath());
    // Same folder (installed, or the build's bin/), then beside a macOS bundle.
    for (const QString &candidate : {appDir.filePath(name), appDir.filePath(QStringLiteral("../../../") + name)}) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.canonicalFilePath();
    }
    return {};
}

Status RenderLink::connectToRunning(int timeoutMs)
{
    if (isConnected())
        return success();
    m_buffer.clear();
    m_replies.clear();
    m_socket.abort();
    m_socket.connectToServer(m_options.serverName);
    if (!m_socket.waitForConnected(timeoutMs)) {
        const QString reason = m_socket.errorString();
        m_socket.abort();
        return linkError(QStringLiteral("No renderer at %1: %2").arg(m_options.serverName, reason));
    }
    return success();
}

Status RenderLink::start()
{
    if (connectToRunning(200).ok()) {
        const auto status = request({{QStringLiteral("cmd"), QStringLiteral("status")}}, 2000);
        if (status.ok() || !status.error().message.contains(QLatin1String("protocol version")))
            return success(); // ours (or busy): it keeps showing what it shows
        retire(status.error().message);
    }

    const QString executable =
        m_options.executable.isEmpty() ? findExecutable() : m_options.executable;
    if (executable.isEmpty())
        return linkError(QStringLiteral("lumen-render was not found next to the app"));

    QProcess process;
    process.setProgram(executable);
    process.setArguments(QStringList{QStringLiteral("--server"), m_options.serverName}
                         + m_options.arguments);
    // The renderer outlives this process, so it must not hold our pipes open.
    const QString log = QDir::temp().filePath(m_options.serverName + QStringLiteral(".log"));
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(log, QIODevice::Truncate);
    process.setStandardErrorFile(log, QIODevice::Append);
    if (!process.startDetached())
        return linkError(QStringLiteral("Could not start %1").arg(executable));

    QDeadlineTimer deadline(m_options.startTimeoutMs);
    while (!deadline.hasExpired()) {
        if (connectToRunning(200).ok())
            return success();
        QThread::msleep(50);
    }
    // The end of the renderer's own output says why, if it said anything.
    QString said;
    if (QFile file(log); file.open(QIODevice::ReadOnly)) {
        if (file.size() > 2000)
            file.seek(file.size() - 2000);
        said = QString::fromUtf8(file.readAll()).trimmed();
    }
    return linkError(QStringLiteral("lumen-render did not start within %1 ms (log: %2)%3")
                         .arg(m_options.startTimeoutMs)
                         .arg(log, said.isEmpty() ? QString() : QStringLiteral(":\n") + said));
}

void RenderLink::drain()
{
    m_buffer += m_socket.readAll();
    qsizetype end;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        const auto reply = decode(line);
        if (!reply) {
            emit replyFailed(QStringLiteral("Unreadable reply from the renderer"));
            continue;
        }
        const QString id = reply->value(QStringLiteral("id")).toString();
        if (m_waiting.contains(id)) {
            m_replies.insert(id, *reply);
        } else if (auto callback = m_callbacks.take(id)) {
            callback(*reply);
        } else if (!reply->value(QStringLiteral("ok")).toBool()) {
            emit replyFailed(reply->value(QStringLiteral("error")).toString());
        }
    }
    if (m_buffer.size() > maxLineBytes) {
        m_buffer.clear();
        emit replyFailed(QStringLiteral("Reply from the renderer is too long"));
    }
}

Status RenderLink::ask(const QJsonObject &message, std::function<void(const QJsonObject &)> onReply)
{
    if (!isConnected())
        return linkError(QStringLiteral("Not connected to the renderer"));
    const QString id = QString::number(m_nextId++);
    QJsonObject numbered = message;
    numbered.insert(QStringLiteral("id"), id);
    // Old unanswered asks are dropped rather than kept for ever.
    if (m_callbacks.size() > 32)
        m_callbacks.clear();
    m_callbacks.insert(id, std::move(onReply));
    if (m_socket.write(encode(numbered)) < 0) {
        m_callbacks.remove(id);
        return linkError(m_socket.errorString());
    }
    m_socket.flush();
    return success();
}

Status RenderLink::send(const QJsonObject &message)
{
    if (!isConnected())
        return linkError(QStringLiteral("Not connected to the renderer"));
    QJsonObject numbered = message;
    numbered.insert(QStringLiteral("id"), QString::number(m_nextId++));
    if (m_socket.write(encode(numbered)) < 0)
        return linkError(m_socket.errorString());
    m_socket.flush();
    return success();
}

Result<QJsonObject> RenderLink::request(const QJsonObject &message, int timeoutMs)
{
    if (!isConnected())
        return linkError(QStringLiteral("Not connected to the renderer"));

    const QString id = QString::number(m_nextId++);
    QJsonObject numbered = message;
    numbered.insert(QStringLiteral("id"), id);
    m_waiting.insert(id);
    m_socket.write(encode(numbered));
    m_socket.flush();

    QDeadlineTimer deadline(timeoutMs);
    while (!m_replies.contains(id)) {
        if (deadline.hasExpired() || !isConnected()) {
            m_waiting.remove(id);
            return linkError(QStringLiteral("No reply from the renderer"));
        }
        if (m_socket.bytesAvailable() == 0)
            m_socket.waitForReadyRead(int(qMax<qint64>(1, deadline.remainingTime())));
        drain();
    }
    m_waiting.remove(id);
    const QJsonObject reply = m_replies.take(id);
    if (!reply.value(QStringLiteral("ok")).toBool())
        return linkError(reply.value(QStringLiteral("error")).toString());
    return reply;
}

void RenderLink::retire(const QString &versionError)
{
    // "Unsupported protocol version 10 (renderer speaks 9)": quit in its words.
    static const QRegularExpression speaks(QStringLiteral("renderer speaks (\\d+)"));
    const auto match = speaks.match(versionError);
    const QJsonObject quit{{QStringLiteral("v"), match.hasMatch() ? match.captured(1).toInt() : protocolVersion},
                           {QStringLiteral("cmd"), QStringLiteral("quit")}};
    qWarning("Replacing a renderer from another version of Lumen (%s)", qPrintable(versionError));
    m_socket.write(QJsonDocument(quit).toJson(QJsonDocument::Compact) + '\n');
    m_socket.flush();
    QDeadlineTimer deadline(3000);
    while (isConnected() && !deadline.hasExpired())
        m_socket.waitForDisconnected(100);
    m_socket.abort();
    // Until its name is free for ours.
    while (!deadline.hasExpired()) {
        QLocalSocket probe;
        probe.connectToServer(m_options.serverName);
        if (!probe.waitForConnected(100))
            break;
        probe.abort();
        QThread::msleep(50);
    }
}

Status RenderLink::quit()
{
    auto reply = request({{QStringLiteral("cmd"), QStringLiteral("quit")}});
    m_socket.disconnectFromServer();
    if (!reply.ok())
        return reply.error();
    return success();
}

} // namespace lumen::core::render
