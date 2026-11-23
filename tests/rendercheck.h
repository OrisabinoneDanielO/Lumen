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

} // namespace lumen::test
