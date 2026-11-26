#pragma once

#include "rowmodel.h"

#include "lumen/core/data/database.h"
#include "lumen/core/data/entities.h"

#include <QObject>
#include <QUrl>

namespace lumen::core::show {
class ShowController;
struct Scene;
}

namespace lumen::app {

// Props (batch 14): overlays on the Props layer that stay up while slides
// change: a lower third (a name and a title, e.g. for the speaker) and a bug
// (a logo in a corner). One of each can be up at a time, shown and hidden
// independently; Clear Props (F3) and Clear All take both down, and nothing
// else touches them.
//
// The props themselves are kept in the library; what is up is read back from
// the scene, so this always agrees with the output.
class PropsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *props READ props CONSTANT)

public:
    explicit PropsController(core::show::ShowController &show, QObject *parent = nullptr);

    void attach(core::data::Database &db);
    QObject *props() { return &m_model; }

    Q_INVOKABLE bool show(int row);
    Q_INVOKABLE bool hide(int row);
    Q_INVOKABLE bool toggle(int row);
    // A lower third; returns false if line1 is empty.
    Q_INVOKABLE bool addLowerThird(const QString &line1, const QString &line2);
    // A bug from a picture file; corner: topLeft, topRight, bottomLeft,
    // bottomRight.
    Q_INVOKABLE bool addBug(const QUrl &image, const QString &corner);
    Q_INVOKABLE bool remove(int row);

private:
    void load();
    void refresh();
    QJsonObject partFor(const core::data::Prop &prop) const;
    static QString partName(core::data::PropKind kind);

    core::show::ShowController &m_show;
    core::data::Database *m_db = nullptr;
    std::vector<core::data::Prop> m_props;
    RowModel m_model{{"name", "kind", "summary", "shown"}};
};

} // namespace lumen::app
