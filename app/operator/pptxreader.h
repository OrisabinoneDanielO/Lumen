#pragma once

#include <QImage>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace lumen::app {

// Reads a PowerPoint (.pptx) file without PowerPoint: the slides in show
// order (from presentation.xml, not the file names inside the zip), which
// are hidden, and a basic picture of each slide. The picture has the slide's
// background, pictures, filled shapes and text with their sizes, colours and
// alignment; effects, charts, tables, SmartArt and exact font metrics are not
// reproduced. Used when no office program is installed to convert the deck.
class PptxReader
{
public:
    PptxReader();
    ~PptxReader();
    PptxReader(const PptxReader &) = delete;
    PptxReader &operator=(const PptxReader &) = delete;

    // Returns false (with error()) if the file is not a readable .pptx.
    bool open(const QString &path);
    QString error() const { return m_error; }

    struct SlideInfo
    {
        QString part; // e.g. "ppt/slides/slide3.xml"
        bool hidden = false;
    };
    const std::vector<SlideInfo> &slides() const { return m_slides; }
    int visibleCount() const;

    // Slide size in points (1/72 inch).
    QSizeF slideSize() const { return m_size; }

    // Draws slide `index` (into slides()) `width` pixels wide.
    QImage render(int index, int width) const;

    struct Impl; // the zip and parsed parts (used by the slide painter)

private:
    std::unique_ptr<Impl> d;
    std::vector<SlideInfo> m_slides;
    QSizeF m_size{720, 405};
    QString m_error;
};

} // namespace lumen::app
