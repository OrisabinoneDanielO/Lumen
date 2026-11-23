#include "lumen/core/render/renderlink.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
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

RenderLink::RenderLink(Options options) : m_options(std::move(options)) {}

RenderLink::~RenderLink()
{
    // Disconnecting without "quit" leaves the output up, which is what a crash
    // does too. A normal exit calls quit() first.
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
    if (connectToRunning(200).ok())
        return success();

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
    return linkError(QStringLiteral("lumen-render did not start within %1 ms (log: %2)")
                         .arg(m_options.startTimeoutMs)
                         .arg(log));
}

Result<QJsonObject> RenderLink::request(const QJsonObject &message, int timeoutMs)
{
    if (!isConnected())
        return linkError(QStringLiteral("Not connected to the renderer"));

    m_socket.write(encode(message));
    if (!m_socket.waitForBytesWritten(timeoutMs) && m_socket.bytesToWrite() > 0)
        return linkError(QStringLiteral("Sending to the renderer timed out"));

    QDeadlineTimer deadline(timeoutMs);
    while (!m_buffer.contains('\n')) {
        if (deadline.hasExpired() || !isConnected())
            return linkError(QStringLiteral("No reply from the renderer"));
        if (m_socket.bytesAvailable() == 0)
            m_socket.waitForReadyRead(int(qMax<qint64>(1, deadline.remainingTime())));
        m_buffer += m_socket.readAll();
        if (m_buffer.size() > maxLineBytes)
            return linkError(QStringLiteral("Reply from the renderer is too long"));
    }

    const qsizetype end = m_buffer.indexOf('\n');
    const QByteArray line = m_buffer.left(end);
    m_buffer.remove(0, end + 1);

    const auto reply = decode(line);
    if (!reply)
        return linkError(QStringLiteral("Unreadable reply from the renderer"));
    if (!reply->value(QStringLiteral("ok")).toBool())
        return linkError(reply->value(QStringLiteral("error")).toString());
    return *reply;
}

Status RenderLink::show(const QColor &background, const QString &text)
{
    auto reply = request({{QStringLiteral("cmd"), QStringLiteral("show")},
                          {QStringLiteral("colour"), background.name(QColor::HexRgb)},
                          {QStringLiteral("text"), text}});
    if (!reply.ok())
        return reply.error();
    return success();
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
