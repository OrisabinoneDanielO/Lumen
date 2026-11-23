#include "keyfill.h"

#include <QPainter>

namespace lumen::render {

QImage fillOf(const QImage &frame)
{
    // A premultiplied picture drawn over black is just its colour values.
    QImage fill(frame.size(), QImage::Format_RGB32);
    fill.fill(Qt::black);
    QPainter painter(&fill);
    painter.drawImage(0, 0, frame);
    return fill;
}

QImage keyOf(const QImage &frame)
{
    const QImage source = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage key(source.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < source.height(); ++y) {
        const auto *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        uchar *out = key.scanLine(y);
        for (int x = 0; x < source.width(); ++x)
            out[x] = uchar(qAlpha(in[x]));
    }
    return key;
}

void PictureWindow::setPicture(const QImage &picture)
{
    m_picture = picture;
    update();
}

void PictureWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(QRect(QPoint(0, 0), size()), Qt::black);
    if (!m_picture.isNull())
        painter.drawImage(QRect(QPoint(0, 0), size()), m_picture);
}

} // namespace lumen::render
