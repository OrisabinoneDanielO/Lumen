#include "lumen/core/midi/mtcinput.h"

#ifdef LUMEN_HAVE_MIDI
#include "RtMidi.h"
#endif

namespace lumen::core::midi {

struct MtcInput::Port
{
#ifdef LUMEN_HAVE_MIDI
    std::unique_ptr<RtMidiIn> in;
#endif
    QString name;
};

#ifdef LUMEN_HAVE_MIDI
namespace {
void received(double, std::vector<unsigned char> *message, void *user)
{
    // RtMidi's thread: hand the bytes to the main thread.
    auto *input = static_cast<MtcInput *>(user);
    const QByteArray bytes(reinterpret_cast<const char *>(message->data()), qsizetype(message->size()));
    QMetaObject::invokeMethod(input, [input, bytes] { emit input->midi(bytes); }, Qt::QueuedConnection);
}
} // namespace
#endif

MtcInput::MtcInput(const QString &device, QObject *parent) : QObject(parent), m_device(device)
{
    scan();
    m_rescan.setInterval(2000);
    connect(&m_rescan, &QTimer::timeout, this, &MtcInput::scan);
    m_rescan.start();
}

MtcInput::~MtcInput()
{
#ifdef LUMEN_HAVE_MIDI
    for (auto &port : m_ports)
        port->in->closePort();
#endif
}

bool MtcInput::available()
{
#ifdef LUMEN_HAVE_MIDI
    return true;
#else
    return false;
#endif
}

void MtcInput::scan()
{
#ifdef LUMEN_HAVE_MIDI
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, "Lumen timecode");
        const unsigned int count = probe.getPortCount();
        for (unsigned int i = 0; i < count; ++i) {
            const QString name = QString::fromStdString(probe.getPortName(i));
            // Not our own ports, not ones already open, only the chosen device.
            if (name.contains(QLatin1String("Lumen")) || m_names.contains(name))
                continue;
            if (!m_device.isEmpty() && !name.contains(m_device, Qt::CaseInsensitive))
                continue;
            auto port = std::make_unique<Port>();
            port->name = name;
            port->in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "Lumen timecode");
            // Timecode is a "time" message and full frames are SysEx; RtMidi
            // leaves both out unless told.
            port->in->ignoreTypes(false, false, true);
            port->in->setCallback(&received, this);
            port->in->openPort(i, "Lumen timecode in");
            m_names << name;
            m_ports.push_back(std::move(port));
        }
    } catch (const RtMidiError &e) {
        qWarning("lumen-render: MIDI timecode: %s", e.what());
        m_rescan.stop();
    }
#endif
}

} // namespace lumen::core::midi
