#pragma once

#include <QImage>
#include <QRasterWindow>

namespace lumen::render {

// Key and fill (batch 15): two pictures that let a vision mixer or switcher
// lay the stream over its own camera picture without alpha support. The
// fill is the stream's picture over black; the key is its transparency as
// grey (white where fully opaque, black where transparent).
QImage fillOf(const QImage &frame);
QImage keyOf(const QImage &frame);

// A window showing a picture scaled to fill it (fullscreen on a screen, or a
// window); used for the key and the fill.
class PictureWindow : public QRasterWindow
{
public:
    using QRasterWindow::QRasterWindow;
    void setPicture(const QImage &picture);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_picture;
};

} // namespace lumen::render
