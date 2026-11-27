#pragma once

// How a theme draws text on the audience output, and fitting text into it.
// Sizes are in pixels on a 1920x1080 output and scale with the real output.

#include <QColor>
#include <QJsonObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

namespace lumen::core::show {

inline constexpr double referenceHeight = 1080.0;
inline const QSizeF referenceSize{1920.0, 1080.0};

struct TextStyle
{
    QString font = QStringLiteral("Noto Sans");
    int weight = 600;       // CSS-style: 400 regular, 600 semibold, 700 bold
    int maxSize = 72;       // largest text size
    int minSize = 40;       // never smaller; longer text is split across slides
    QColor colour = Qt::white;
    QString align = QStringLiteral("center");   // left | center | right
    QString valign = QStringLiteral("middle");  // top | middle | bottom
    double lineHeight = 1.2;
    int outline = 0;        // outline width in pixels (0: none)
    QColor outlineColour = Qt::black;
    bool shadow = true;
    QRectF safeArea{0.05, 0.05, 0.9, 0.9}; // fractions of the output

    QString footerPosition = QStringLiteral("below"); // below | above | none
    int footerSize = 28;
    QColor footerColour{0xF2, 0xC3, 0x8B};

    QJsonObject toJson() const;
    static TextStyle fromJson(const QJsonObject &json); // missing fields keep defaults
    bool operator==(const TextStyle &) const = default;
};

// The output's bundled fonts (Noto Sans, Noto Serif: SIL OFL 1.1, wide Latin
// coverage including Yoruba, Igbo and Hausa; Noto Sans Devanagari and
// Malayalam stand in for letters they lack). Registers them with Qt once;
// returns their family names.
QStringList registerOutputFonts();

// Where a style puts body text and its footer on an output of the given size.
struct TextAreas
{
    QRectF body;
    QRectF footer; // empty if the footer is off or there is no footer text
};
TextAreas textAreas(const TextStyle &style, const QSizeF &output, bool hasFooter);

// The largest size from maxSize down to minSize at which text fits the body
// area of a 1920x1080 output. fits is false if it does not fit even at
// minSize (then the caller should split the text across slides).
struct Fit
{
    int pixelSize = 0;
    bool fits = false;
};
Fit fitText(const QString &text, const TextStyle &style, bool hasFooter);

// Splits text that does not fit into parts that do, at sentence, then clause,
// then word boundaries. Returns the text unchanged (one part) if it fits.
QStringList splitToFit(const QString &text, const TextStyle &style, bool hasFooter, int maxParts = 6);

} // namespace lumen::core::show
