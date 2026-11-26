#include "livecontrol.h"

#include "operatorcontroller.h"
#include "rowmodel.h"

#include <QJsonArray>

namespace lumen::app {

LiveControl::LiveControl(OperatorController &op, QObject *parent) : QObject(parent), m_op(op) {}

QStringList LiveControl::commands()
{
    return {"next", "previous", "nextItem", "previousItem", "item", "slide", "section", "group",
            "clear", "clearAll", "logo", "black", "message", "hideMessage"};
}

bool LiveControl::run(const QString &command, const QJsonObject &arguments, QString *error)
{
    const auto failed = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (m_op.mode() == QLatin1String("edit") && command != QLatin1String("item"))
        return failed(tr("Lumen is in Edit mode"));
    bool done = true;
    if (command == QLatin1String("next")) {
        done = m_op.next();
    } else if (command == QLatin1String("previous")) {
        done = m_op.previous();
    } else if (command == QLatin1String("nextItem")) {
        m_op.nextItem();
    } else if (command == QLatin1String("previousItem")) {
        m_op.previousItem();
    } else if (command == QLatin1String("item")) {
        const int index = arguments.value("index").toInt(-1);
        if (index < 0 || index >= m_op.plan()->property("count").toInt())
            return failed(tr("No plan item %1").arg(index));
        m_op.selectItem(index);
    } else if (command == QLatin1String("slide")) {
        done = m_op.goLive(arguments.value("index").toInt(-1));
    } else if (command == QLatin1String("section")) {
        done = m_op.jumpToSection(arguments.value("number").toInt());
    } else if (command == QLatin1String("group")) {
        done = m_op.jumpToGroup(arguments.value("label").toString());
    } else if (command == QLatin1String("clear")) {
        static const QStringList layers{"background", "slide", "props", "messages"};
        const QString layer = arguments.value("layer").toString();
        if (!layers.contains(layer))
            return failed(tr("Unknown layer \"%1\"").arg(layer));
        m_op.clearLayer(layer);
    } else if (command == QLatin1String("clearAll")) {
        m_op.clearAll();
    } else if (command == QLatin1String("logo")) {
        m_op.toggleLogo();
    } else if (command == QLatin1String("black")) {
        m_op.toggleBlack();
    } else if (command == QLatin1String("message")) {
        done = m_op.showMessage(arguments.value("text").toString(), arguments.value("ticker").toBool());
    } else if (command == QLatin1String("hideMessage")) {
        m_op.hideMessage();
    } else {
        return failed(tr("Unknown command \"%1\"").arg(command));
    }
    if (!done)
        return failed(tr("Nothing to do"));
    return true;
}

QJsonObject LiveControl::state() const
{
    QJsonArray plan;
    auto *rows = qobject_cast<RowModel *>(m_op.plan());
    for (int i = 0; i < rows->rowCount(); ++i) {
        const QVariantMap row = rows->get(i);
        plan.append(QJsonObject{{"title", row.value("title").toString()},
                                {"kind", row.value("kind").toString()},
                                {"header", row.value("header").toBool()}});
    }
    QJsonArray slides;
    auto *slideRows = qobject_cast<RowModel *>(m_op.slides());
    for (int i = 0; i < slideRows->rowCount(); ++i) {
        const QVariantMap row = slideRows->get(i);
        slides.append(QJsonObject{{"group", row.value("group").toString()}, {"text", row.value("text").toString()}});
    }
    return {{"planName", m_op.planName()},
            {"plan", plan},
            {"currentItem", m_op.currentItem()},
            {"itemTitle", m_op.itemTitle()},
            {"slides", slides},
            {"liveItem", m_op.liveItem()},
            {"liveSlide", m_op.liveSlide()},
            {"previewSlide", m_op.previewSlide()},
            {"liveText", m_op.liveText()},
            {"liveLabel", m_op.liveLabel()},
            {"previewText", m_op.previewText()},
            {"logo", m_op.logoOn()},
            {"black", m_op.blackOn()},
            {"message", m_op.messageShown()},
            {"mode", m_op.mode()}};
}

} // namespace lumen::app
