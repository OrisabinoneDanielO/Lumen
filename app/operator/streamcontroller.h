#pragma once

#include <QObject>
#include <QString>

namespace lumen::app {

// The stream output's settings (batch 15): Slide and Props on transparency,
// drawn by a render process of its own and sent as key and fill and/or NDI.
// The app starts and stops that process when asked (toggleRequested and
// restartRequested); this only holds the choices and the state.
class StreamController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool keyFill READ keyFill WRITE setKeyFill NOTIFY changed)
    Q_PROPERTY(bool ndiAvailable READ ndiAvailable CONSTANT) // experimental/ndi
    Q_PROPERTY(bool ndi READ ndi WRITE setNdi NOTIFY changed)
    Q_PROPERTY(QString ndiName READ ndiName WRITE setNdiName NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    explicit StreamController(bool ndiAvailable, QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    bool keyFill() const { return m_keyFill; }
    bool ndiAvailable() const { return m_ndiAvailable; }
    bool ndi() const { return m_ndi && m_ndiAvailable; }
    QString ndiName() const { return m_ndiName; }
    QString status() const { return m_status; }

    void setKeyFill(bool on);
    void setNdi(bool on);
    void setNdiName(const QString &name);
    void setConnected(bool connected, const QString &status = {});

    // The arguments for lumen-render --stream that match the choices.
    QStringList renderArguments() const;

    Q_INVOKABLE void toggle() { emit toggleRequested(!m_connected); }

signals:
    void changed();
    void toggleRequested(bool on);
    void restartRequested(); // a choice changed while the stream runs

private:
    void choiceChanged();

    bool m_ndiAvailable;
    bool m_connected = false;
    bool m_keyFill = true;
    bool m_ndi = false;
    QString m_ndiName = QStringLiteral("Lumen Presenter");
    QString m_status;
};

} // namespace lumen::app
