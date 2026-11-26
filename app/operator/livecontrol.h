#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace lumen::app {

class OperatorController;

// The live actions other devices can ask for (batch 19): the phone remote
// (HTTP), OSC and MIDI all come through here, so they do exactly what the
// keyboard and the window do (and, like them, never send anything live in
// Edit mode).
//
// Commands (with arguments in a JSON object):
//   next, previous, nextItem, previousItem
//   item {"index"}         select a plan item (0-based)
//   slide {"index"}        send a slide of the selected item live
//   section {"number"}     the Nth section (1-based), as the number keys
//   group {"label"}        the next section starting with a label, e.g. "Chorus"
//   clear {"layer"}        background, slide, props, messages
//   clearAll, logo, black
//   message {"text"}, hideMessage
class LiveControl : public QObject
{
    Q_OBJECT

public:
    explicit LiveControl(OperatorController &op, QObject *parent = nullptr);

    static QStringList commands();

    // Runs a command; false (with *error) if it is unknown or did nothing.
    bool run(const QString &command, const QJsonObject &arguments = {}, QString *error = nullptr);

    // What a remote shows: the plan, the selected item's slides, what is
    // live and what is next.
    QJsonObject state() const;

signals:
    // A command came from a device (for the operator's status line).
    void ran(const QString &command, const QString &from);

private:
    OperatorController &m_op;
};

} // namespace lumen::app
