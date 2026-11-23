#include "lumen/core/show/textstyle.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QTextLayout>

#include <cmath>

namespace lumen::core::show {

QJsonObject TextStyle::toJson() const
{
    return {{"font", font},
            {"weight", weight},
            {"maxSize", maxSize},
            {"minSize", minSize},
            {"colour", colour.name(QColor::HexArgb)},
            {"align", align},
            {"valign", valign},
            {"lineHeight", lineHeight},
            {"outline", outline},
            {"outlineColour", outlineColour.name(QColor::HexArgb)},
            {"shadow", shadow},
            {"safeArea", QJsonObject{{"x", safeArea.x()}, {"y", safeArea.y()},
                                     {"w", safeArea.width()}, {"h", safeArea.height()}}},
            {"footerPosition", footerPosition},
            {"footerSize", footerSize},
            {"footerColour", footerColour.name(QColor::HexArgb)}};
}

TextStyle TextStyle::fromJson(const QJsonObject &json)
{
    TextStyle s;
    auto colour = [&json](const char *key, const QColor &fallback) {
        const QColor c = QColor::fromString(json.value(QLatin1String(key)).toString());
        return c.isValid() ? c : fallback;
    };
    s.font = json.value("font").toString(s.font);
    s.weight = json.value("weight").toInt(s.weight);
    s.maxSize = qMax(8, json.value("maxSize").toInt(s.maxSize));
    s.minSize = qBound(8, json.value("minSize").toInt(s.minSize), s.maxSize);
    s.colour = colour("colour", s.colour);
    s.align = json.value("align").toString(s.align);
    s.valign = json.value("valign").toString(s.valign);
    s.lineHeight = json.value("lineHeight").toDouble(s.lineHeight);
    s.outline = json.value("outline").toInt(s.outline);
    s.outlineColour = colour("outlineColour", s.outlineColour);
    s.shadow = json.value("shadow").toBool(s.shadow);
    if (json.contains("safeArea")) {
        const QJsonObject a = json.value("safeArea").toObject();
        const QRectF area(a.value("x").toDouble(), a.value("y").toDouble(), a.value("w").toDouble(),
                          a.value("h").toDouble());
        if (area.width() > 0.1 && area.height() > 0.1 && QRectF(0, 0, 1, 1).contains(area))
            s.safeArea = area;
    }
    s.footerPosition = json.value("footerPosition").toString(s.footerPosition);
    s.footerSize = qMax(8, json.value("footerSize").toInt(s.footerSize));
    s.footerColour = colour("footerColour", s.footerColour);
    return s;
}

namespace {

// Fonts and text layout need a GUI application (not, e.g., a console tool).
bool canUseFonts()
{
    return qobject_cast<QGuiApplication *>(QCoreApplication::instance()) != nullptr;
}

} // namespace

QStringList registerOutputFonts()
{
    if (!canUseFonts())
        return {};
    static const QStringList families = [] {
        QStringList out;
        for (const char *file : {"NotoSans-Regular", "NotoSans-Bold", "NotoSerif-Regular", "NotoSerif-Bold"}) {
            const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/lumen/fonts/%1.ttf").arg(QLatin1String(file)));
            if (id >= 0) {
                for (const QString &family : QFontDatabase::applicationFontFamilies(id)) {
                    if (!out.contains(family))
                        out << family;
                }
            }
        }
        return out;
    }();
    return families;
}

TextAreas textAreas(const TextStyle &style, const QSizeF &output, bool hasFooter)
{
    const double scale = output.height() / referenceHeight;
    const QRectF safe(style.safeArea.x() * output.width(), style.safeArea.y() * output.height(),
                      style.safeArea.width() * output.width(), style.safeArea.height() * output.height());
    TextAreas areas{safe, {}};
    if (!hasFooter || style.footerPosition == QLatin1String("none"))
        return areas;
    const double footerHeight = std::ceil(style.footerSize * 1.6 * scale);
    const double gap = std::ceil(style.footerSize * 0.5 * scale);
    if (style.footerPosition == QLatin1String("above")) {
        areas.footer = QRectF(safe.left(), safe.top(), safe.width(), footerHeight);
        areas.body = QRectF(safe.left(), safe.top() + footerHeight + gap, safe.width(),
                            safe.height() - footerHeight - gap);
    } else {
        areas.footer = QRectF(safe.left(), safe.bottom() - footerHeight, safe.width(), footerHeight);
        areas.body = QRectF(safe.left(), safe.top(), safe.width(), safe.height() - footerHeight - gap);
    }
    return areas;
}

namespace {

QFont styleFont(const TextStyle &style, int pixelSize)
{
    registerOutputFonts();
    QFont font(style.font);
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::Weight(qBound(100, style.weight, 900)));
    return font;
}

// Height the text takes when wrapped to width at the given size, with the
// style's line spacing (as Qt Quick's proportional lineHeight applies it).
double wrappedHeight(const QString &text, const QFont &font, double width, double lineHeight)
{
    const QFontMetricsF metrics(font);
    double height = 0;
    for (const QString &paragraph : text.split(u'\n')) {
        QTextLayout layout(paragraph.isEmpty() ? QStringLiteral(" ") : paragraph, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        int lines = 0;
        double widest = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(width);
            widest = qMax(widest, line.naturalTextWidth());
            ++lines;
        }
        layout.endLayout();
        if (widest > width + 0.5)
            return 1e9; // a word wider than the box
        height += lines * metrics.height() * lineHeight;
    }
    return height;
}

bool fitsAt(const QString &text, const TextStyle &style, const QRectF &body, int size)
{
    // Leave a little room for the outline and differences between renderers.
    const QFont font = styleFont(style, size);
    const double slack = 0.97;
    return wrappedHeight(text, font, body.width() * slack - 2.0 * style.outline, style.lineHeight)
           <= body.height() * slack;
}

} // namespace

Fit fitText(const QString &text, const TextStyle &style, bool hasFooter)
{
    if (!canUseFonts())
        return {style.maxSize, true}; // cannot measure; the renderer still shrinks to fit
    const QRectF body = textAreas(style, referenceSize, hasFooter).body;
    if (fitsAt(text, style, body, style.maxSize))
        return {style.maxSize, true};
    if (!fitsAt(text, style, body, style.minSize))
        return {style.minSize, false};
    int low = style.minSize;   // fits
    int high = style.maxSize;  // does not
    while (high - low > 1) {
        const int mid = (low + high) / 2;
        (fitsAt(text, style, body, mid) ? low : high) = mid;
    }
    return {low, true};
}

QStringList splitToFit(const QString &text, const TextStyle &style, bool hasFooter, int maxParts)
{
    if (fitText(text, style, hasFooter).fits || maxParts <= 1)
        return {text};

    // Candidate break points, best first: sentence ends, then clauses, then
    // any space. Choose the one nearest the middle.
    static const QRegularExpression sentence(QStringLiteral(R"([.!?;:]["”’)]*\s)"));
    static const QRegularExpression clause(QStringLiteral(R"([,–—]\s)"));
    static const QRegularExpression space(QStringLiteral(R"(\s)"));
    const qsizetype middle = text.size() / 2;
    for (const QRegularExpression *pattern : {&sentence, &clause, &space}) {
        qsizetype best = -1;
        for (auto it = pattern->globalMatch(text); it.hasNext();) {
            const qsizetype at = it.next().capturedEnd();
            if (at <= 0 || at >= text.size())
                continue;
            if (best < 0 || std::abs(at - middle) < std::abs(best - middle))
                best = at;
        }
        // Avoid very lopsided parts.
        if (best > text.size() / 5 && best < text.size() * 4 / 5) {
            const int half = qMax(1, maxParts / 2);
            return splitToFit(text.left(best).trimmed(), style, hasFooter, half)
                   + splitToFit(text.mid(best).trimmed(), style, hasFooter, maxParts - half);
        }
    }
    return {text};
}

} // namespace lumen::core::show
