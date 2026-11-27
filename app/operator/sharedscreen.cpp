#include "sharedscreen.h"

#include "sharing.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QUrl>

#include <memory>

namespace lumen::app {

using namespace core::show;

namespace {

const QString refPrefix = QStringLiteral("lumen-file:");
constexpr int timeoutMs = 5000;

QByteArray line(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

// A file name the screen may store: the key made by refFor, nothing else.
bool safeKey(const QString &key)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^[0-9a-f]{16}-[^/\\:]{1,200}$)"));
    return pattern.match(key).hasMatch() && !key.contains(QLatin1String(".."));
}

QStringList changedLayers(const Scene &now, const Scene &before)
{
    QStringList changed;
    for (int i = 0; i < layerCount; ++i) {
        if (!(now.layers[std::size_t(i)] == before.layers[std::size_t(i)]))
            changed << layerName(Layer(i));
    }
    if (now.logo != before.logo)
        changed << QStringLiteral("logo");
    if (now.black != before.black)
        changed << QStringLiteral("black");
    if (!(now.audio == before.audio))
        changed << QStringLiteral("audio");
    return changed;
}

} // namespace

void rewriteFiles(Content &content, const std::function<QString(const QString &path)> &map)
{
    const auto mapPath = [&](QString &path) {
        if (!path.isEmpty())
            path = map(path);
    };
    // URLs (file://) in JSON: mapped as local paths, written back as URLs;
    // references stay as they are.
    const auto mapUrl = [&](QJsonObject &object, const QString &key) {
        const QString value = object.value(key).toString();
        if (value.isEmpty())
            return;
        const QUrl url(value);
        const QString path = value.startsWith(refPrefix) ? value : url.isLocalFile() ? url.toLocalFile() : QString();
        if (path.isEmpty())
            return;
        const QString mapped = map(path);
        object.insert(key, mapped.startsWith(refPrefix) ? mapped : QUrl::fromLocalFile(mapped).toString());
    };
    mapPath(content.videoPath);
    mapPath(content.imagePath);
    QJsonArray elements;
    for (const QJsonValue &value : content.elements) {
        QJsonObject element = value.toObject();
        mapUrl(element, QStringLiteral("image"));
        mapUrl(element, QStringLiteral("video"));
        elements.append(element);
    }
    content.elements = elements;
    if (content.prop.contains(QStringLiteral("bug"))) {
        QJsonObject bug = content.prop.value(QStringLiteral("bug")).toObject();
        mapUrl(bug, QStringLiteral("image"));
        content.prop.insert(QStringLiteral("bug"), bug);
    }
}

// ---- ScreenHost ----------------------------------------------------------------

ScreenHost::ScreenHost(ShowController &show, QString folder, QObject *parent)
    : QObject(parent), m_show(show), m_folder(std::move(folder))
{}

QJsonObject ScreenHost::handle(const QString &cmd, const QJsonObject &request, const QByteArray &payload)
{
    if (cmd == QLatin1String("screenState")) {
        QJsonObject reply{{"ok", true}, {"scene", toJson(m_show.scene())}};
        if (m_extra) {
            const QJsonObject extra = m_extra();
            for (auto it = extra.begin(); it != extra.end(); ++it)
                reply.insert(it.key(), it.value());
        }
        return reply;
    }
    if (cmd == QLatin1String("screenFile")) {
        const QString key = request.value("ref").toString().mid(refPrefix.size());
        if (!request.value("ref").toString().startsWith(refPrefix) || !safeKey(key))
            return {{"ok", false}, {"error", "bad file name"}};
        if (!QDir().mkpath(m_folder))
            return {{"ok", false}, {"error", "cannot store files"}};
        QFile file(QDir(m_folder).filePath(key));
        if (!file.open(QIODevice::WriteOnly) || file.write(payload) != payload.size())
            return {{"ok", false}, {"error", "cannot store the file"}};
        return {{"ok", true}};
    }
    if (cmd == QLatin1String("screenVideo")) {
        const QString action = request.value("action").toString();
        if (action != QLatin1String("pause") && action != QLatin1String("play") && action != QLatin1String("seek"))
            return {{"ok", false}, {"error", "unknown video action"}};
        emit videoControlRequested(action, request.value("positionMs").toInteger());
        return {{"ok", true}};
    }
    if (cmd != QLatin1String("screen"))
        return {{"ok", false}, {"error", "unknown screen command"}};

    const auto sent = sceneFromJson(request.value("scene").toObject());
    if (!sent)
        return {{"ok", false}, {"error", "unreadable scene"}};
    const QStringList changed = request.value("changed").toVariant().toStringList();
    QStringList missing;
    std::vector<std::pair<Layer, Content>> contents;
    std::vector<std::pair<Layer, Transition>> transitions;
    for (const QString &name : changed) {
        const auto layer = layerFromName(name);
        if (!layer)
            continue;
        LayerState state = sent->layer(*layer);
        rewriteFiles(state.content, [&](const QString &path) {
            if (!path.startsWith(refPrefix))
                return path; // a file that is already here (this computer's)
            const QString key = path.mid(refPrefix.size());
            if (!safeKey(key))
                return QString();
            const QString local = QDir(m_folder).filePath(key);
            if (!QFileInfo::exists(local) && !missing.contains(path))
                missing << path;
            return local;
        });
        contents.push_back({*layer, state.content});
        transitions.push_back({*layer, state.transition});
    }
    // The music beside the slides, its file named the same way.
    Content audio = sent->audio;
    const bool audioChanged = changed.contains(QStringLiteral("audio"));
    if (audioChanged) {
        rewriteFiles(audio, [&](const QString &path) {
            if (!path.startsWith(refPrefix))
                return path;
            const QString key = path.mid(refPrefix.size());
            if (!safeKey(key))
                return QString();
            const QString local = QDir(m_folder).filePath(key);
            if (!QFileInfo::exists(local) && !missing.contains(path))
                missing << path;
            return local;
        });
    }
    if (!missing.isEmpty())
        return {{"ok", true}, {"missing", QJsonArray::fromStringList(missing)}, {"scene", toJson(m_show.scene())}};
    if (audioChanged)
        m_show.setAudio(audio);
    for (const auto &[layer, transition] : transitions)
        m_show.setTransition(layer, transition);
    if (!contents.empty())
        m_show.setContents(contents);
    if (changed.contains(QStringLiteral("logo")))
        m_show.setLogo(sent->logo);
    if (changed.contains(QStringLiteral("black")))
        m_show.setBlack(sent->black);
    emit changedByOther();
    return {{"ok", true}, {"scene", toJson(m_show.scene())}};
}

// ---- ScreenLink ----------------------------------------------------------------

ScreenLink::ScreenLink(ShowController &show, QObject *parent) : QObject(parent), m_show(show)
{
    connect(&m_show, &ShowController::sceneChanged, this, &ScreenLink::sceneChanged);
    m_poll.setInterval(1000);
    connect(&m_poll, &QTimer::timeout, this, &ScreenLink::poll);
}

bool ScreenLink::start(Sharing *sharing)
{
    if (!sharing)
        return false;
    const Sharing::Connection c = sharing->connection();
    if (c.host.isNull() || c.port == 0 || !sharing->remoteTakesScreen()) {
        fail(tr("Connect to a computer that lets others send to its screen first"));
        return false;
    }
    return start(c.host, c.port, c.code, c.name);
}

bool ScreenLink::start(const QHostAddress &host, quint16 port, const QString &code, const QString &name)
{
    m_host = host;
    m_port = port;
    m_code = code;
    m_target = name.isEmpty() ? host.toString() : name;
    m_active = true;
    m_busy = false;
    m_dirty = false;
    m_status = tr("Joining %1's screen…").arg(m_target);
    emit changed();
    // First what is on that screen: this window shows it, and sends only
    // what changes from now on.
    request({{"cmd", "screenState"}}, {}, [this](const QJsonObject &reply, const QString &error) {
        if (!m_active)
            return;
        if (!error.isEmpty())
            return fail(error);
        adoptHost(reply.value("scene").toObject());
        m_status = tr("Live goes to %1's screen").arg(m_target);
        emit changed();
        m_poll.start();
    });
    return true;
}

void ScreenLink::stop()
{
    if (!m_active)
        return;
    m_active = false;
    m_poll.stop();
    m_status = tr("Live stays on this computer");
    emit changed();
    emit remoteStatus({});
}

void ScreenLink::videoControl(const QString &action, qint64 positionMs)
{
    if (!m_active)
        return;
    request({{"cmd", "screenVideo"}, {"action", action}, {"positionMs", positionMs}}, {},
            [this](const QJsonObject &, const QString &error) {
                if (!error.isEmpty())
                    fail(error);
            });
}

void ScreenLink::fail(const QString &message)
{
    m_status = m_target.isEmpty() ? message : tr("%1: %2").arg(m_target, message);
    emit changed();
}

void ScreenLink::request(const QJsonObject &request, const QByteArray &payload, Callback done)
{
    // Never waits: a screen that does not answer must not hold up this one.
    auto *socket = new QTcpSocket(this);
    auto buffer = std::make_shared<QByteArray>();
    auto finished = std::make_shared<bool>(false);
    auto finish = [socket, finished, done](const QJsonObject &reply, const QString &error) {
        if (*finished)
            return;
        *finished = true;
        socket->abort();
        socket->deleteLater();
        done(reply, error);
    };
    QTimer::singleShot(payload.size() > 1024 * 1024 ? 10 * 60 * 1000 : timeoutMs, socket,
                       [finish] { finish({}, tr("it did not answer")); });
    connect(socket, &QTcpSocket::connected, socket, [socket, request, payload, code = m_code] {
        QJsonObject header = request;
        header.insert("lumen", 1);
        header.insert("code", code);
        if (!payload.isEmpty())
            header.insert("size", payload.size());
        socket->write(line(header));
        socket->write(payload);
    });
    connect(socket, &QTcpSocket::readyRead, socket, [socket, buffer, finish] {
        buffer->append(socket->readAll());
        const qsizetype end = buffer->indexOf('\n');
        if (end < 0)
            return;
        const QJsonObject reply = QJsonDocument::fromJson(buffer->left(end)).object();
        if (!reply.value("ok").toBool()) {
            const QString error = reply.value("error").toString();
            finish(reply, error == QLatin1String("wrong code") ? tr("that is not its code") : error);
        } else {
            finish(reply, {});
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, socket, [socket, finish](QAbstractSocket::SocketError error) {
        if (error != QAbstractSocket::RemoteHostClosedError)
            finish({}, tr("it cannot be reached (%1)").arg(socket->errorString()));
    });
    socket->connectToHost(m_host, m_port);
}

void ScreenLink::sceneChanged()
{
    if (!m_active || m_applying)
        return;
    m_dirty = true;
    send();
}

QString ScreenLink::refFor(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return path;
    // Names the file by where it is, its size and when it was changed: a
    // new version travels again, the same one only once.
    const QByteArray id = QStringLiteral("%1|%2|%3")
                              .arg(info.absoluteFilePath())
                              .arg(info.size())
                              .arg(info.lastModified().toMSecsSinceEpoch())
                              .toUtf8();
    const QString key = QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex().left(16))
                        + u'-' + info.fileName().remove(u'/').remove(u'\\').remove(u':').left(200);
    m_refs.insert(key, info.absoluteFilePath());
    return refPrefix + key;
}

Content ScreenLink::toRefs(Content content)
{
    rewriteFiles(content, [this](const QString &path) { return path.startsWith(refPrefix) ? path : refFor(path); });
    return content;
}

Scene ScreenLink::toLocal(Scene scene) const
{
    // Files that came from here show from here; the screen's own files keep
    // its paths (this window shows no picture for them).
    const auto map = [this](const QString &path) {
        const QString key = QFileInfo(path).fileName();
        return m_refs.contains(key) ? m_refs.value(key) : path;
    };
    for (LayerState &state : scene.layers)
        rewriteFiles(state.content, map);
    rewriteFiles(scene.audio, map);
    return scene;
}

void ScreenLink::adoptHost(const QJsonObject &sceneJson)
{
    const auto host = sceneFromJson(sceneJson);
    if (!host)
        return;
    m_lastHost = toLocal(*host);
    if (m_dirty)
        return; // changes here still to send: they win
    Scene mine = m_lastHost;
    m_applying = true;
    m_show.replaceScene(mine);
    m_applying = false;
}

void ScreenLink::send()
{
    if (!m_active || m_busy || !m_dirty)
        return;
    const Scene now = m_show.scene();
    const QStringList layers = changedLayers(now, m_lastHost);
    m_dirty = false;
    if (layers.isEmpty())
        return;
    Scene outgoing = now;
    for (LayerState &state : outgoing.layers)
        state.content = toRefs(state.content);
    outgoing.audio = toRefs(outgoing.audio);
    m_busy = true;
    request({{"cmd", "screen"}, {"scene", toJson(outgoing)}, {"changed", QJsonArray::fromStringList(layers)}}, {},
            [this](const QJsonObject &reply, const QString &error) {
                m_busy = false;
                if (!m_active)
                    return;
                if (!error.isEmpty()) {
                    m_dirty = true; // tried again with the next poll
                    return fail(error);
                }
                const QStringList missing = reply.value("missing").toVariant().toStringList();
                if (!missing.isEmpty()) {
                    m_dirty = true;
                    return upload(missing);
                }
                if (m_status != tr("Live goes to %1's screen").arg(m_target)) {
                    m_status = tr("Live goes to %1's screen").arg(m_target);
                    emit changed();
                }
                adoptHost(reply.value("scene").toObject());
                send(); // anything changed meanwhile
            });
}

void ScreenLink::upload(QStringList refs)
{
    if (refs.isEmpty()) {
        send();
        return;
    }
    const QString ref = refs.takeFirst();
    const QString local = m_refs.value(ref.mid(refPrefix.size()));
    QFile file(local);
    if (local.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        fail(tr("%1 cannot be read").arg(QFileInfo(local).fileName()));
        return;
    }
    m_busy = true;
    m_status = tr("Copying %1 to %2…").arg(QFileInfo(local).fileName(), m_target);
    emit changed();
    request({{"cmd", "screenFile"}, {"ref", ref}}, file.readAll(), [this, refs](const QJsonObject &, const QString &error) {
        m_busy = false;
        if (!m_active)
            return;
        if (!error.isEmpty())
            return fail(error);
        upload(refs);
    });
}

void ScreenLink::poll()
{
    if (!m_active)
        return;
    if (m_dirty) {
        send();
        return;
    }
    if (m_busy)
        return;
    request({{"cmd", "screenState"}}, {}, [this](const QJsonObject &reply, const QString &error) {
        if (!m_active)
            return;
        if (!error.isEmpty())
            return fail(error);
        if (m_status != tr("Live goes to %1's screen").arg(m_target)) {
            m_status = tr("Live goes to %1's screen").arg(m_target);
            emit changed();
        }
        if (!m_busy)
            adoptHost(reply.value("scene").toObject());
        // The screen's live video, in this computer's paths.
        QJsonObject video = reply.value("videoPosition").toObject();
        if (!video.isEmpty()) {
            const QString key = QFileInfo(video.value("path").toString()).fileName();
            if (m_refs.contains(key))
                video.insert("path", m_refs.value(key));
        }
        emit remoteStatus(video.isEmpty() ? QJsonObject{} : QJsonObject{{"videoPosition", video}});
    });
}

} // namespace lumen::app
