#include "messagetemplates.h"

#include "operatorcontroller.h"

#include "lumen/core/data/repository.h"

#include <QRegularExpression>

namespace lumen::app {

using namespace core::data;

namespace {

const QRegularExpression fieldPattern(QStringLiteral(R"(\{([^{}]+)\})"));

} // namespace

MessageTemplates::MessageTemplates(OperatorController &op, QObject *parent) : QObject(parent), m_op(op) {}

void MessageTemplates::attach(Database &db)
{
    m_db = &db;
    auto existing = Repository<Message>(db).list();
    if (existing.ok() && existing.value().empty()) {
        for (const auto &[name, text] : {std::pair{tr("Nursery"), tr("Parent of {number}, please come to the nursery")},
                                         std::pair{tr("Car"), tr("Car {plate}: your lights are on")},
                                         std::pair{tr("Welcome"), tr("Welcome, {name}!")}})
            add(name, text);
    }
    load();
}

QStringList MessageTemplates::fieldsOf(const QString &templateText)
{
    QStringList fields;
    for (auto it = fieldPattern.globalMatch(templateText); it.hasNext();) {
        const QString field = it.next().captured(1).trimmed();
        if (!field.isEmpty() && !fields.contains(field))
            fields << field;
    }
    return fields;
}

QString MessageTemplates::fill(const QString &templateText, const QVariantMap &values, QString *missing)
{
    QString out;
    qsizetype last = 0;
    for (auto it = fieldPattern.globalMatch(templateText); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString value = values.value(match.captured(1).trimmed()).toString().simplified();
        if (value.isEmpty()) {
            if (missing)
                *missing = match.captured(1).trimmed();
            return {};
        }
        out += templateText.mid(last, match.capturedStart() - last) + value;
        last = match.capturedEnd();
    }
    return out + templateText.mid(last);
}

void MessageTemplates::load()
{
    m_messages.clear();
    if (m_db) {
        if (auto all = Repository<Message>(*m_db).list(); all.ok())
            m_messages = all.value();
    }
    QList<QVariantMap> rows;
    for (const Message &m : m_messages)
        rows << QVariantMap{{"name", m.name}, {"text", m.templateText}, {"fields", fieldsOf(m.templateText)}};
    m_model.setRows(rows);
}

QStringList MessageTemplates::fields(int row) const
{
    return row >= 0 && row < int(m_messages.size()) ? fieldsOf(m_messages[size_t(row)].templateText) : QStringList();
}

bool MessageTemplates::show(int row, const QVariantMap &values, bool ticker)
{
    if (row < 0 || row >= int(m_messages.size()))
        return false;
    QString missing;
    const QString text = fill(m_messages[size_t(row)].templateText, values, &missing);
    m_message = text.isEmpty() ? tr("Fill in \"%1\" first").arg(missing) : QString();
    emit messageChanged();
    return !text.isEmpty() && m_op.showMessage(text, ticker);
}

bool MessageTemplates::add(const QString &name, const QString &templateText)
{
    if (!m_db || name.trimmed().isEmpty() || templateText.trimmed().isEmpty())
        return false;
    QJsonArray fields;
    for (const QString &field : fieldsOf(templateText))
        fields.append(QJsonObject{{"name", field}});
    if (!Repository<Message>(*m_db).create({.name = name.trimmed(), .templateText = templateText.trimmed(), .fields = fields}).ok())
        return false;
    load();
    return true;
}

bool MessageTemplates::remove(int row)
{
    if (!m_db || row < 0 || row >= int(m_messages.size()))
        return false;
    if (!Repository<Message>(*m_db).remove(m_messages[size_t(row)].id).ok())
        return false;
    load();
    return true;
}

} // namespace lumen::app
