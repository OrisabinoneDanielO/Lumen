#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace lumen::app {

// Optional update checks (design doc: "update checks are optional"). Off
// until the operator checks or turns on "Check when Lumen starts"; it then
// asks GitHub for the newest published release and says whether it is newer.
// It never downloads or installs anything: "Download" opens the release page
// in the browser, so nothing changes during a service.
class UpdateChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    struct Options
    {
        // GitHub's "latest release" (drafts and pre-releases left out).
        QUrl url{QStringLiteral("https://api.github.com/repos/OrisabinoneDanielO/Lumen/releases/latest")};
        QString currentVersion; // empty: this build's version
        int timeoutMs = 15000;
    };

    explicit UpdateChecker(QObject *parent = nullptr);
    UpdateChecker(Options options, QObject *parent = nullptr);
    ~UpdateChecker() override;

    bool automatic() const { return m_automatic; }
    void setAutomatic(bool on);
    bool checking() const { return m_reply != nullptr; }
    bool available() const { return m_available; }
    QString latestVersion() const { return m_latest; }
    QString status() const { return m_status; }
    QUrl downloadUrl() const { return m_downloadUrl; }

    Q_INVOKABLE void check();
    // Opens the newest release's page in the browser.
    Q_INVOKABLE void openDownload();

    // < 0 if a is older than b, 0 if the same, > 0 if newer. "v1.2.3",
    // "1.2" and "1.2.3-beta" are understood; missing parts count as 0.
    static int compareVersions(const QString &a, const QString &b);

signals:
    void changed();
    // A check finished and a newer version exists.
    void updateFound(const QString &version);

private:
    void finished();
    void fail(const QString &reason);

    Options m_options;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_automatic = false;
    bool m_available = false;
    QString m_latest;
    QString m_status;
    QUrl m_downloadUrl;
};

} // namespace lumen::app
