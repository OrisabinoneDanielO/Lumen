#pragma once

#include "rowmodel.h"

#include "lumen/core/data/database.h"
#include "lumen/core/data/entities.h"

#include <QObject>
#include <QVariantMap>

namespace lumen::app {

class OperatorController;

// Message templates with fields (batch 14): "Parent of {number}, please come
// to the nursery". The operator picks one, fills in its fields and shows it
// in the banner or as a ticker. Fields are the names in braces; the library
// keeps the templates (Message entities).
class MessageTemplates : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *templates READ templates CONSTANT)
    // Why the last Show did nothing ("Fill in number first"); empty after one
    // that worked.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    explicit MessageTemplates(OperatorController &op, QObject *parent = nullptr);

    // Loads the library's templates, adding three common ones if it has none.
    void attach(core::data::Database &db);
    QObject *templates() { return &m_model; }

    // The field names in a template's text, in order, each once.
    static QStringList fieldsOf(const QString &templateText);
    // The text with each {field} replaced; empty if a field has no value.
    // The template with its fields filled; empty if one is empty (its name in
    // *missing).
    static QString fill(const QString &templateText, const QVariantMap &values, QString *missing = nullptr);
    QString message() const { return m_message; }

    Q_INVOKABLE QStringList fields(int row) const;
    Q_INVOKABLE bool show(int row, const QVariantMap &values, bool ticker);
    Q_INVOKABLE bool add(const QString &name, const QString &templateText);
    Q_INVOKABLE bool remove(int row);

signals:
    void messageChanged();

private:
    void load();

    OperatorController &m_op;
    core::data::Database *m_db = nullptr;
    std::vector<core::data::Message> m_messages;
    RowModel m_model{{"name", "text", "fields"}};
    QString m_message;
};

} // namespace lumen::app
