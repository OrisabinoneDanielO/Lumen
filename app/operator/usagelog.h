#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"

#include <QDate>
#include <QSet>
#include <QString>

#include <vector>

namespace lumen::app {

// Song usage for CCLI licence reporting: each song shown live is counted
// once a day, on this computer only. The report is a CSV file the church
// submits itself (design doc: "tallied locally and exported").
class UsageLog
{
public:
    explicit UsageLog(core::data::Database &db) : m_db(db) {}

    // A song went live (repeats on the same day are counted once).
    core::Status record(const QString &presentationId, QDate day = QDate::currentDate());

    struct Row
    {
        QString title;
        QString author;
        QString ccli;
        QString copyright;
        int uses = 0; // days shown
        QDate first;
        QDate last;
    };
    // Songs shown between the two dates (inclusive), most used first.
    core::Result<std::vector<Row>> report(QDate from, QDate to) const;
    // The report as CSV (Title, Author, CCLI song number, Copyright, Times
    // used, First used, Last used).
    core::Status exportCsv(const QString &file, QDate from, QDate to) const;

private:
    core::data::Database &m_db;
    QSet<QString> m_recorded; // presentation id + day, this session
};

} // namespace lumen::app
