#pragma once

#include "rowmodel.h"

#include <QJsonArray>
#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

namespace lumen::app {

class LiveControl;

// MIDI control (batch 19): a MIDI controller (pads, a keyboard, a foot
// switch) drives the slides. Every MIDI input on the computer is listened
// to while this is on (new ones are picked up as they are plugged in).
//
// Each action has a MIDI message; the defaults, on any channel:
//   C4 (note 60) next slide, B3 (59) previous slide, D4 (62) next item,
//   A3 (57) previous item, E4 (64) clear slide, F4 (65) clear all,
//   G4 (67) black, A4 (69) logo, and a program change N selects plan item N.
// A note acts when pressed; a control change when it rises to 64 or more.
// "Learn" gives an action whatever message comes next.
class MidiInput : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT) // built with MIDI
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QStringList devices READ devices NOTIFY changed)
    Q_PROPERTY(QObject *mappings READ mappings CONSTANT)
    Q_PROPERTY(QString learning READ learning NOTIFY changed)
    Q_PROPERTY(QString last READ last NOTIFY changed) // the last message, e.g. "Note C4"

public:
    explicit MidiInput(LiveControl &control, QObject *parent = nullptr);
    ~MidiInput() override;

    static bool available();
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QStringList devices() const;
    QObject *mappings() { return &m_model; }
    QString learning() const { return m_learning; }
    QString last() const { return m_last; }

    Q_INVOKABLE void learn(const QString &action); // empty: stop learning
    Q_INVOKABLE void resetMappings();

    // Saved and restored as JSON ([{"action", "kind", "number", "channel"}]).
    QJsonArray mappingsJson() const;
    void setMappingsJson(const QJsonArray &json);

    // One MIDI message (status byte first), as from a device; public so
    // tests can feed messages without hardware.
    void handle(const std::vector<unsigned char> &message);

    // "Note C4", "CC 20", "Program 3".
    static QString describe(const QString &kind, int number);

signals:
    void changed();
    void mappingsChanged();

private:
    struct Mapping
    {
        QString action; // next, previous, nextItem, previousItem, clearSlide, clearAll, black, logo, programItem
        QString kind;   // note, cc, program
        int number = 0;
        int channel = -1; // 0-15, or -1 for any
    };
    void rescan();
    void refreshModel();
    bool run(const QString &action, int value);

    LiveControl &m_control;
    bool m_enabled = false;
    std::vector<Mapping> m_mappings;
    QString m_learning;
    QString m_last;
    QTimer m_rescan;
    struct Ports;
    std::unique_ptr<Ports> m_ports;
    QHash<int, int> m_ccValues; // last value per (channel << 8 | controller)
    RowModel m_model{{"action", "label", "message"}};
};

} // namespace lumen::app
