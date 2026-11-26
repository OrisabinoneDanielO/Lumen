#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <vector>

namespace lumen::core::midi {

// MIDI timecode input (timecode cues that follow another device): the render
// process follows it, and the operator records cues against it. Listens on every MIDI input whose name contains `device` (all of
// them when empty), and looks again every two seconds for one plugged in
// later. Each message arrives on the main thread as midi(bytes).
class MtcInput : public QObject
{
    Q_OBJECT

public:
    explicit MtcInput(const QString &device, QObject *parent = nullptr);
    ~MtcInput() override;

    // Built with a MIDI system (RtMidi).
    static bool available();
    QStringList openPorts() const { return m_names; }

signals:
    void midi(const QByteArray &bytes);

private:
    void scan();

    QString m_device;
    QStringList m_names;
    struct Port;
    std::vector<std::unique_ptr<Port>> m_ports;
    QTimer m_rescan;
};

} // namespace lumen::core::midi
