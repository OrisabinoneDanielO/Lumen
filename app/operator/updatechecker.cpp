#include "updatechecker.h"

#include "lumen/core/buildinfo.h"

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>

namespace lumen::app {

namespace {

constexpr auto automaticKey = "updates/checkAtStart";

QList<int> versionParts(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('v')) || text.startsWith(QLatin1Char('V')))
        text.remove(0, 1);
    // "1.2.3-beta.1" and "1.2.3+build": only the numbers before them count.
    text = text.section(QRegularExpression(QStringLiteral("[-+ ]")), 0, 0);
    QList<int> parts;
    for (const QString &part : text.split(QLatin1Char('.'))) {
        bool ok = false;
        const int n = part.toInt(&ok);
        parts << (ok ? n : 0);
    }
    return parts;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent) : UpdateChecker(Options{}, parent) {}

UpdateChecker::UpdateChecker(Options options, QObject *parent) : QObject(parent), m_options(std::move(options))
{
    if (m_options.currentVersion.isEmpty())
        m_options.currentVersion = core::version();
    m_automatic = QSettings().value(QLatin1String(automaticKey), false).toBool();
}

UpdateChecker::~UpdateChecker()
{
    if (m_reply)
        m_reply->abort();
}

void UpdateChecker::setAutomatic(bool on)
{
    if (on == m_automatic)
        return;
    m_automatic = on;
    QSettings().setValue(QLatin1String(automaticKey), on);
    emit changed();
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    QList<int> x = versionParts(a);
    QList<int> y = versionParts(b);
    while (x.size() < y.size())
        x << 0;
    while (y.size() < x.size())
        y << 0;
    for (int i = 0; i < x.size(); ++i) {
        if (x.at(i) != y.at(i))
            return x.at(i) < y.at(i) ? -1 : 1;
    }
    return 0;
}

void UpdateChecker::check()
{
    if (m_reply)
        return;
    // Created on first use: no network object at all until the operator asks.
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(m_options.url);
    // Names the program only; nothing about the user or the computer.
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LumenPresenter/%1").arg(m_options.currentVersion));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(m_options.timeoutMs);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::finished);
    m_status = tr("Checking for updates…");
    emit changed();
}

void UpdateChecker::fail(const QString &reason)
{
    m_status = tr("Could not check for updates: %1").arg(reason);
    emit changed();
}

void UpdateChecker::finished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError && httpStatus != 404) {
        fail(reply->error() == QNetworkReply::OperationCanceledError ? tr("no answer in time") : reply->errorString());
        return;
    }
    if (httpStatus == 404) {
        // No release published yet.
        m_available = false;
        m_status = tr("Lumen %1 is the newest version.").arg(m_options.currentVersion);
        emit changed();
        return;
    }
    const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty() || versionParts(tag).value(0, -1) < 0) {
        fail(tr("the answer was not understood"));
        return;
    }
    m_latest = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
    m_downloadUrl = QUrl(release.value(QStringLiteral("html_url")).toString());
    m_available = compareVersions(m_latest, m_options.currentVersion) > 0;
    m_status = m_available ? tr("Lumen %1 is available (this is %2).").arg(m_latest, m_options.currentVersion)
                           : tr("Lumen %1 is the newest version.").arg(m_options.currentVersion);
    emit changed();
    if (m_available)
        emit updateFound(m_latest);
}

void UpdateChecker::openDownload()
{
    if (m_downloadUrl.isValid() && m_downloadUrl.scheme() == QLatin1String("https"))
        QDesktopServices::openUrl(m_downloadUrl);
}

} // namespace lumen::app
