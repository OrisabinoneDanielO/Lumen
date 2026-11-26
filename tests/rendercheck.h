#pragma once

// Pixel checks shared by the render tests.

#include <QColor>
#include <QImage>

namespace lumen::test {

// True if the top-left corner (background, away from any text) has colour.
inline bool backgroundIs(const QImage &frame, const QColor &colour)
{
    if (frame.isNull() || frame.width() < 8 || frame.height() < 8)
        return false;
    const QColor corner = frame.pixelColor(4, 4);
    auto close = [](int a, int b) { return qAbs(a - b) <= 2; };
    return close(corner.red(), colour.red()) && close(corner.green(), colour.green())
           && close(corner.blue(), colour.blue());
}

// Number of near-white pixels: the text is white on a dark background.
inline int textPixels(const QImage &frame)
{
    int count = 0;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const QColor c = frame.pixelColor(x, y);
            if (c.red() > 200 && c.green() > 200 && c.blue() > 200)
                ++count;
        }
    }
    return count;
}

// Colour at a point given as fractions of the width and height.
inline QColor colourAt(const QImage &frame, double xFraction, double yFraction)
{
    if (frame.isNull())
        return {};
    const int x = qBound(0, int(xFraction * frame.width()), frame.width() - 1);
    const int y = qBound(0, int(yFraction * frame.height()), frame.height() - 1);
    return frame.pixelColor(x, y);
}

inline bool near(const QColor &a, const QColor &b, int tolerance = 3)
{
    return a.isValid() && b.isValid() && qAbs(a.red() - b.red()) <= tolerance
           && qAbs(a.green() - b.green()) <= tolerance && qAbs(a.blue() - b.blue()) <= tolerance;
}

// a and b mixed: fraction 0 is a, 1 is b.
inline QColor mix(const QColor &a, const QColor &b, double fraction)
{
    auto lerp = [fraction](int x, int y) { return int(x + (y - x) * fraction + 0.5); };
    return QColor(lerp(a.red(), b.red()), lerp(a.green(), b.green()), lerp(a.blue(), b.blue()));
}

// Pixels with any channel above level (text that is partly faded counts).
inline int pixelsBrighterThan(const QImage &frame, int level)
{
    int count = 0;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const QColor c = frame.pixelColor(x, y);
            if (qMax(c.red(), qMax(c.green(), c.blue())) > level)
                ++count;
        }
    }
    return count;
}

// Text pixels inside a horizontal band (fractions of the height).
inline int textPixelsBetween(const QImage &frame, double top, double bottom)
{
    const int y0 = int(top * frame.height());
    const int y1 = int(bottom * frame.height());
    return textPixels(frame.copy(0, y0, frame.width(), qMax(1, y1 - y0)));
}

} // namespace lumen::test
