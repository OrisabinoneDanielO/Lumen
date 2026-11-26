#include "midiinput.h"

#include "livecontrol.h"

#include <QJsonObject>
#include <QPointer>

#ifdef LUMEN_HAVE_MIDI
#include <RtMidi.h>
#endif

namespace lumen::app {

namespace {

const QList<std::pair<QString, QString>> &actions()
{
    static const QList<std::pair<QString, QString>> list{
        {"next", QT_TRANSLATE_NOOP("MidiInput", "Next slide")},
        {"previous", QT_TRANSLATE_NOOP("MidiInput", "Previous slide")},
        {"nextItem", QT_TRANSLATE_NOOP("MidiInput", "Next item")},
        {"previousItem", QT_TRANSLATE_NOOP("MidiInput", "Previous item")},
        {"clearSlide", QT_TRANSLATE_NOOP("MidiInput", "Clear slide")},
        {"clearAll", QT_TRANSLATE_NOOP("MidiInput", "Clear all")},
        {"black", QT_TRANSLATE_NOOP("MidiInput", "Black")},
        {"logo", QT_TRANSLATE_NOOP("MidiInput", "Logo")},
        {"programItem", QT_TRANSLATE_NOOP("MidiInput", "Plan item (program change)")},
    };
    return list;
}

} // namespace

// The open MIDI inputs (one per device), by device name.
struct MidiInput::Ports
{
#ifdef LUMEN_HAVE_MIDI
    struct Port
    {
        QString name;
        std::unique_ptr<QPointer<MidiInput>> target; // what the callback reaches
        std::unique_ptr<RtMidiIn> in;                // closed before target goes
    };
    std::vector<Port> open;
#endif
};

MidiInput::MidiInput(LiveControl &control, QObject *parent) : QObject(parent), m_control(control), m_ports(std::make_unique<Ports>())
{
    resetMappings();
    m_rescan.setInterval(3000);
    connect(&m_rescan, &QTimer::timeout, this, &MidiInput::rescan);
}

MidiInput::~MidiInput()
{
    setEnabled(false);
}

bool MidiInput::available()
{
#ifdef LUMEN_HAVE_MIDI
    return true;
#else
    return false;
#endif
}

QString MidiInput::describe(const QString &kind, int number)
{
    if (kind == QLatin1String("note")) {
        static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return tr("Note %1%2").arg(QLatin1String(names[number % 12])).arg(number / 12 - 1);
    }
    if (kind == QLatin1String("cc"))
        return tr("CC %1").arg(number);
    return tr("Program change");
}

void MidiInput::resetMappings()
{
    m_mappings = {{"next", "note", 60}, {"previous", "note", 59}, {"nextItem", "note", 62}, {"previousItem", "note", 57},
                  {"clearSlide", "note", 64}, {"clearAll", "note", 65}, {"black", "note", 67}, {"logo", "note", 69},
                  {"programItem", "program", 0}};
    refreshModel();
    emit mappingsChanged();
}

QJsonArray MidiInput::mappingsJson() const
{
    QJsonArray out;
    for (const Mapping &m : m_mappings)
        out.append(QJsonObject{{"action", m.action}, {"kind", m.kind}, {"number", m.number}, {"channel", m.channel}});
    return out;
}

void MidiInput::setMappingsJson(const QJsonArray &json)
{
    if (json.isEmpty())
        return;
    m_mappings.clear();
    for (const QJsonValue &v : json) {
        const QJsonObject o = v.toObject();
        m_mappings.push_back({o.value("action").toString(), o.value("kind").toString(), o.value("number").toInt(),
                              o.value("channel").toInt(-1)});
    }
    refreshModel();
}

void MidiInput::refreshModel()
{
    QList<QVariantMap> rows;
    for (const auto &[action, label] : actions()) {
        QString message = tr("(none)");
        for (const Mapping &m : m_mappings) {
            if (m.action == action)
                message = describe(m.kind, m.number);
        }
        rows << QVariantMap{{"action", action}, {"label", tr(label.toUtf8().constData())}, {"message", message}};
    }
    m_model.updateRows(rows);
}

void MidiInput::learn(const QString &action)
{
    m_learning = action;
    emit changed();
}

QStringList MidiInput::devices() const
{
    QStringList names;
#ifdef LUMEN_HAVE_MIDI
    for (const auto &port : m_ports->open)
        names << port.name;
#endif
    return names;
}

void MidiInput::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    if (on) {
        rescan();
        m_rescan.start();
    } else {
        m_rescan.stop();
#ifdef LUMEN_HAVE_MIDI
        m_ports->open.clear();
#endif
    }
    emit changed();
}

void MidiInput::rescan()
{
#ifdef LUMEN_HAVE_MIDI
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, "Lumen Presenter probe");
        QStringList present;
        for (unsigned int i = 0; i < probe.getPortCount(); ++i) {
            const QString name = QString::fromStdString(probe.getPortName(i));
            // Not our own ports (and not the system's "through" port).
            if (name.contains(QLatin1String("Lumen Presenter")) || name.contains(QLatin1String("Midi Through")))
                continue;
            present << name;
            const bool known = std::any_of(m_ports->open.begin(), m_ports->open.end(), [&](const auto &p) { return p.name == name; });
            if (known)
                continue;
            auto port = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "Lumen Presenter");
            auto target = std::make_unique<QPointer<MidiInput>>(this);
            port->setCallback(
                [](double, std::vector<unsigned char> *message, void *data) {
                    // RtMidi's thread: hand the message to the UI thread.
                    auto *target = static_cast<QPointer<MidiInput> *>(data);
                    const std::vector<unsigned char> copy = *message;
                    if (*target)
                        QMetaObject::invokeMethod(target->data(), [target, copy] {
                            if (*target)
                                (*target)->handle(copy);
                        }, Qt::QueuedConnection);
                },
                target.get());
            port->ignoreTypes(true, true, true); // no sysex, timing or active sensing
            port->openPort(i, "Lumen Presenter in");
            m_ports->open.push_back({name, std::move(target), std::move(port)});
        }
        // Unplugged devices.
        const auto before = m_ports->open.size();
        std::erase_if(m_ports->open, [&](const auto &p) { return !present.contains(p.name); });
        if (before != m_ports->open.size() || !present.isEmpty())
            emit changed();
    } catch (const RtMidiError &error) {
        m_last = tr("MIDI: %1").arg(QString::fromStdString(error.getMessage()));
        emit changed();
    }
#endif
}

void MidiInput::handle(const std::vector<unsigned char> &message)
{
    if (message.empty())
        return;
    const int status = message[0] & 0xF0;
    const int channel = message[0] & 0x0F;
    QString kind;
    int number = 0;
    int value = 127;
    if (status == 0x90 && message.size() >= 3 && message[2] > 0) {
        kind = QStringLiteral("note");
        number = message[1];
    } else if (status == 0xB0 && message.size() >= 3) {
        kind = QStringLiteral("cc");
        number = message[1];
        value = message[2];
        // Act on the rise past the middle (a pedal or button pressed).
        const int key = channel << 8 | number;
        const int before = m_ccValues.value(key, 0);
        m_ccValues.insert(key, value);
        if (!(before < 64 && value >= 64) && m_learning.isEmpty())
            return;
    } else if (status == 0xC0 && message.size() >= 2) {
        kind = QStringLiteral("program");
        number = message[1];
    } else {
        return; // note off and the rest
    }
    m_last = describe(kind, number) + (kind == QLatin1String("program") ? QStringLiteral(" %1").arg(number + 1) : QString());

    if (!m_learning.isEmpty()) {
        // This message is now the action's (and no other action's).
        std::erase_if(m_mappings, [&](const Mapping &m) { return m.action == m_learning || (m.kind == kind && m.number == number); });
        m_mappings.push_back({m_learning, kind, kind == QLatin1String("program") ? 0 : number});
        m_learning.clear();
        refreshModel();
        emit mappingsChanged();
        emit changed();
        return;
    }
    emit changed();
    for (const Mapping &m : m_mappings) {
        if (m.kind != kind || (m.channel >= 0 && m.channel != channel))
            continue;
        if (kind == QLatin1String("program") && m.action == QLatin1String("programItem")) {
            run(m.action, number);
            return;
        }
        if (m.number == number) {
            run(m.action, value);
            return;
        }
    }
}

bool MidiInput::run(const QString &action, int value)
{
    QString command = action;
    QJsonObject arguments;
    if (action == QLatin1String("clearSlide")) {
        command = QStringLiteral("clear");
        arguments = {{"layer", "slide"}};
    } else if (action == QLatin1String("programItem")) {
        command = QStringLiteral("item");
        arguments = {{"index", value}}; // program 1 (0 on the wire) is the first item
    }
    const bool done = m_control.run(command, arguments);
    if (done)
        emit m_control.ran(command, tr("MIDI"));
    return done;
}

} // namespace lumen::app
