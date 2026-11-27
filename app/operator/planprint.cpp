#include "planprint.h"

#include <QFileInfo>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTextDocument>

namespace lumen::app {

using namespace core;

namespace {

QString esc(const QString &text)
{
    return text.toHtmlEscaped();
}

// Each group once, in the order it first plays (a chorus sung three times
// is printed once), with its slides' lines.
std::vector<std::pair<QString, QStringList>> groupsOf(const ItemView &item, bool chords)
{
    std::vector<std::pair<QString, QStringList>> groups;
    QString previous;
    for (const SlideView &slide : item.slides) {
        const bool repeat = slide.group != previous
                            && std::any_of(groups.begin(), groups.end(), [&](const auto &g) { return g.first == slide.group; });
        previous = slide.group;
        if (repeat)
            continue;
        if (groups.empty() || groups.back().first != slide.group)
            groups.push_back({slide.group, {}});
        const QString text = chords && !slide.chords.isEmpty() ? slide.chords : slide.text;
        for (const QString &line : text.split(u'\n'))
            groups.back().second << line;
    }
    return groups;
}

// A ChordPro line as two monospaced lines: chords above their syllables.
QString chordLines(const QString &line)
{
    static const QRegularExpression chord(QStringLiteral(R"(\[([^\[\]\s]{1,12})\])"));
    QString above;
    QString words;
    qsizetype from = 0;
    auto it = chord.globalMatch(line);
    while (it.hasNext()) {
        const auto m = it.next();
        words += line.mid(from, m.capturedStart() - from);
        if (above.size() > words.size())
            words += QString(above.size() - words.size(), u' ');
        above += QString(words.size() - above.size(), u' ') + m.captured(1) + u' ';
        from = m.capturedEnd();
    }
    words += line.mid(from);
    if (above.trimmed().isEmpty())
        return esc(words);
    return QStringLiteral("<span style=\"color:#B4560A;font-weight:bold\">%1</span>\n%2").arg(esc(above.trimmed().isEmpty() ? QString() : above), esc(words));
}

} // namespace

QString planHtml(const PlanView &plan, PrintKind kind)
{
    const QString heading = kind == PrintKind::ServiceOrder ? QObject::tr("Order of service")
                            : kind == PrintKind::Lyrics     ? QObject::tr("Lyrics")
                                                            : QObject::tr("Chord charts");
    QString html = QStringLiteral("<html><body style=\"font-family:'Noto Sans'; font-size:11pt\">");
    html += QStringLiteral("<h1 style=\"font-size:18pt;margin-bottom:0\">%1</h1>").arg(esc(plan.name));
    html += QStringLiteral("<p style=\"color:#555;margin-top:2px\">%1 · %2</p>")
                .arg(esc(heading), esc(QLocale().toString(plan.date, QLocale::LongFormat)));

    if (kind == PrintKind::ServiceOrder) {
        html += QStringLiteral("<table width=\"100%\" cellpadding=\"5\" style=\"border-collapse:collapse\">");
        int number = 0;
        for (const ItemView &item : plan.items) {
            if (item.presentationId.isEmpty()) { // a header
                html += QStringLiteral("<tr><td colspan=\"3\" style=\"padding-top:12px;font-weight:bold;font-size:12pt;"
                                       "border-bottom:1px solid #999\">%1</td></tr>").arg(esc(item.title.toUpper()));
                continue;
            }
            QStringList detail;
            if (!item.key.isEmpty())
                detail << QObject::tr("Key %1").arg(item.key);
            if (!item.subtitle.isEmpty())
                detail << item.subtitle;
            html += QStringLiteral("<tr><td width=\"6%\" style=\"color:#777\">%1</td><td width=\"14%\" style=\"color:#777;font-size:9pt\">%2</td>"
                                   "<td><b>%3</b>%4%5</td></tr>")
                        .arg(++number)
                        .arg(esc(item.kind), esc(item.title),
                             detail.isEmpty() ? QString() : QStringLiteral("<br><span style=\"color:#555;font-size:9pt\">%1</span>").arg(esc(detail.join(QStringLiteral(" · ")))),
                             item.notes.isEmpty() ? QString() : QStringLiteral("<br><i style=\"color:#1F5FBF\">%1</i>").arg(esc(item.notes)));
        }
        html += QStringLiteral("</table>");
    } else {
        bool first = true;
        for (const ItemView &item : plan.items) {
            const bool song = item.kind == QLatin1String("SONG");
            const bool scripture = item.kind == QLatin1String("BIBLE");
            if (!song && !(scripture && kind == PrintKind::Lyrics))
                continue;
            if (kind == PrintKind::ChordCharts && !std::any_of(item.slides.begin(), item.slides.end(),
                                                                [](const SlideView &s) { return !s.chords.isEmpty(); }))
                continue; // a song without chords has no chart
            html += QStringLiteral("<h2 style=\"font-size:15pt;margin-bottom:0;%1\">%2</h2>")
                        .arg(first ? QString() : QStringLiteral("page-break-before:always"), esc(item.title));
            first = false;
            QStringList detail;
            if (!item.key.isEmpty())
                detail << QObject::tr("Key %1").arg(item.key);
            // The credit (author, copyright), not the operator's notes on it
            // (its theme).
            for (const QString &part : item.detail.split(QStringLiteral(" · "), Qt::SkipEmptyParts)) {
                if (!part.startsWith(QObject::tr("Theme:")) && !part.startsWith(QLatin1String("Theme:")))
                    detail << part;
            }
            if (!detail.isEmpty())
                html += QStringLiteral("<p style=\"color:#555;font-size:9pt;margin-top:2px\">%1</p>").arg(esc(detail.join(QStringLiteral(" · "))));
            if (item.chips.size() > 1) {
                QStringList order;
                for (const ArrangementChip &chip : item.chips)
                    order << chip.label;
                html += QStringLiteral("<p style=\"font-size:9pt\"><b>%1</b> %2</p>").arg(esc(QObject::tr("Order:")), esc(order.join(u' ')));
            }
            for (const auto &[group, lines] : groupsOf(item, kind == PrintKind::ChordCharts)) {
                if (!group.isEmpty() && !scripture)
                    html += QStringLiteral("<p style=\"margin-bottom:2px;font-weight:bold;color:#444\">%1</p>").arg(esc(group));
                if (kind == PrintKind::ChordCharts) {
                    QStringList out;
                    for (const QString &line : lines)
                        out << chordLines(line);
                    html += QStringLiteral("<pre style=\"font-family:'Noto Sans Mono','DejaVu Sans Mono',monospace;font-size:10.5pt;margin-top:0\">%1</pre>")
                                .arg(out.join(u'\n'));
                } else {
                    QStringList out;
                    for (const QString &line : lines)
                        out << esc(line);
                    html += QStringLiteral("<p style=\"margin-top:0;line-height:130%\">%1</p>").arg(out.join(QStringLiteral("<br>")));
                }
            }
        }
        if (first)
            html += QStringLiteral("<p>%1</p>").arg(esc(kind == PrintKind::ChordCharts ? QObject::tr("No song in this plan has chords.")
                                                                                     : QObject::tr("No songs or scripture in this plan.")));
    }
    html += QStringLiteral("</body></html>");
    return html;
}

Status writePlanPdf(const PlanView &plan, PrintKind kind, const QString &file)
{
    QPdfWriter writer(file);
    if (!writer.setPageSize(QPageSize(QLocale().measurementSystem() == QLocale::ImperialUSSystem ? QPageSize::Letter : QPageSize::A4)))
        return Error{Error::Code::Invalid, QStringLiteral("The page size could not be set")};
    writer.setPageMargins(QMarginsF(18, 18, 18, 18), QPageLayout::Millimeter);
    writer.setTitle(plan.name);
    writer.setCreator(QStringLiteral("Lumen Presenter"));
    QTextDocument document;
    document.setHtml(planHtml(plan, kind));
    document.print(&writer);
    if (!QFileInfo(file).exists() || QFileInfo(file).size() == 0)
        return Error{Error::Code::Invalid, QStringLiteral("%1 could not be written").arg(file)};
    return success();
}

} // namespace lumen::app
