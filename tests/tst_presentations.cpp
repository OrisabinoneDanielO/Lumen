#include "operatorharness.h"

#include "deckimporter.h"
#include "pptxreader.h"
#include "presentationlibrary.h"

#include "lumen/core/data/database.h"
#include "lumen/core/show/scene.h"

#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace lumen;
using namespace lumen::app;

// Batch 10 "Done when": sample decks import with the right slide count and
// order. The sample PowerPoint decks (tests/data/decks, made by
// make_decks.py) give every slide its own background colour and name the
// slide files inside the zip out of order, so the corner pixel of each
// imported picture shows whether the show order was kept.
namespace {

const QString decks = QStringLiteral(LUMEN_DECKS_DIR);

// order-16x9.pptx in show order, without its hidden fifth slide.
const QList<QColor> orderColours{QColor(0xC6, 0x28, 0x28), QColor(0x2E, 0x7D, 0x32), QColor(0x15, 0x65, 0xC0),
                                 QColor(0x6A, 0x1B, 0x9A), QColor(0x00, 0x83, 0x8F)};
const QList<QColor> classicColours{QColor(0xEF, 0x6C, 0x00), QColor(0x37, 0x47, 0x4F), QColor(0x55, 0x8B, 0x2F)};

bool near(QColor a, QColor b, int tolerance = 8)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
           && std::abs(a.blue() - b.blue()) <= tolerance;
}

QString describe(QColor c)
{
    return c.name();
}

// Checks each picture's top-left corner against the expected colours.
// LUMEN_DECK_DUMP=<folder> keeps copies of the pictures, to look at by eye.
void checkOrder(const QStringList &slides, const QList<QColor> &expected, QSize size)
{
    if (const QString dump = qEnvironmentVariable("LUMEN_DECK_DUMP"); !dump.isEmpty()) {
        QDir().mkpath(dump);
        for (const QString &slide : slides) {
            const QString name = QDir(dump).filePath(QFileInfo(QFileInfo(slide).path()).fileName() + u'-'
                                                     + QString::number(qHash(slide), 16) + u'-' + QFileInfo(slide).fileName());
            QFile::copy(slide, name);
        }
    }
    QCOMPARE(slides.size(), expected.size());
    for (int i = 0; i < slides.size(); ++i) {
        const QImage image(slides[i]);
        QVERIFY2(!image.isNull(), qPrintable(slides[i]));
        QCOMPARE(image.size(), size);
        const QColor corner = image.pixelColor(8, 8);
        QVERIFY2(near(corner, expected[i]),
                 qPrintable(QStringLiteral("slide %1 is %2, expected %3").arg(i + 1).arg(describe(corner), describe(expected[i]))));
    }
}

// Pixels in `area` that differ from the background (text or pictures).
int marks(const QImage &image, QRect area, QColor background)
{
    int count = 0;
    area = area.intersected(image.rect());
    for (int y = area.top(); y <= area.bottom(); y += 2) {
        for (int x = area.left(); x <= area.right(); x += 2)
            count += near(image.pixelColor(x, y), background, 40) ? 0 : 1;
    }
    return count;
}

// A PDF with one coloured page per colour; `portrait` pages are upright.
QString makePdf(const QString &folder, const QList<QColor> &colours, int portrait)
{
    const QString path = QDir(folder).filePath(QStringLiteral("sermon.pdf"));
    QPdfWriter writer(path);
    const QPageSize wide(QSizeF(254, 142.875), QPageSize::Millimeter, QStringLiteral("16:9"));
    const QPageSize upright(QSizeF(142.875, 254), QPageSize::Millimeter, QStringLiteral("9:16"));
    writer.setPageSize(portrait == 0 ? upright : wide);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter(&writer);
    for (int i = 0; i < colours.size(); ++i) {
        if (i > 0) {
            writer.setPageSize(i == portrait ? upright : wide);
            writer.newPage();
        }
        painter.fillRect(QRect(QPoint(0, 0), writer.pageLayout().paintRectPixels(writer.resolution()).size()), colours[i]);
        painter.setPen(Qt::white);
        QFont font;
        font.setPixelSize(200);
        painter.setFont(font);
        painter.drawText(QPoint(400, 600), QStringLiteral("Page %1").arg(i + 1));
    }
    painter.end();
    return path;
}

} // namespace

class TestPresentations : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Office programs are tried only where a test asks for them.
        qputenv("LUMEN_POWERPOINT", "none");
    }

    void readerFollowsShowOrder()
    {
        PptxReader reader;
        QVERIFY2(reader.open(decks + QStringLiteral("/order-16x9.pptx")), qPrintable(reader.error()));
        QCOMPARE(int(reader.slides().size()), 6);
        QCOMPARE(reader.visibleCount(), 5);
        const QStringList parts{"ppt/slides/slide4.xml", "ppt/slides/slide1.xml", "ppt/slides/slide6.xml",
                                "ppt/slides/slide2.xml", "ppt/slides/slide5.xml", "ppt/slides/slide3.xml"};
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(reader.slides()[size_t(i)].part, parts[i]);
            QCOMPARE(reader.slides()[size_t(i)].hidden, i == 4);
        }
        QCOMPARE(reader.slideSize(), QSizeF(960, 540));
    }

    void pdfPagesBecomeSlidesInOrder()
    {
        QTemporaryDir work;
        const QList<QColor> colours{QColor(0xB7, 0x1C, 0x1C), QColor(0x1B, 0x5E, 0x20), QColor(0x0D, 0x47, 0xA1),
                                    QColor(0xF5, 0x7F, 0x17)};
        const QString pdf = makePdf(work.path(), colours, 2);
        const DeckImporter::Result result = DeckImporter::import(pdf, work.filePath(QStringLiteral("out")));
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.method, QStringLiteral("pdf"));
        QCOMPARE(result.slides.size(), 4);
        QCOMPARE(result.thumbnails.size(), 4);
        for (int i = 0; i < 4; ++i) {
            const QImage image(result.slides[i]);
            // Wide pages fill 1920 x 1080; the upright one fits the height.
            if (i == 2) {
                QCOMPARE(image.height(), 1080);
                QVERIFY(image.width() < 700);
            } else {
                QCOMPARE(image.size(), QSize(1920, 1080));
            }
            QVERIFY2(near(image.pixelColor(8, 8), colours[i]),
                     qPrintable(QStringLiteral("page %1 is %2").arg(i + 1).arg(describe(image.pixelColor(8, 8)))));
            QVERIFY(!QImage(result.thumbnails[i]).isNull());
        }
    }

    // Without an office program Lumen draws the slides itself.
    void pptxDrawnByLumen()
    {
        QTemporaryDir work;
        DeckImporter::Options options;
        options.office = DeckImporter::Office::Never;
        const DeckImporter::Result result =
            DeckImporter::import(decks + QStringLiteral("/order-16x9.pptx"), work.path(), options);
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.method, QStringLiteral("basic"));
        QCOMPARE(result.hiddenSkipped, 1);
        checkOrder(result.slides, orderColours, QSize(1920, 1080));

        // Slide 1: the title (placed by the master, through the layout) and
        // the bullets are drawn where the master puts them.
        const QImage welcome(result.slides[0]);
        QVERIFY(marks(welcome, QRect(160, 54, 1600, 216), orderColours[0]) > 200);  // title
        QVERIFY(marks(welcome, QRect(160, 324, 1600, 648), orderColours[0]) > 200); // bullets
        QCOMPARE(marks(welcome, QRect(0, 1000, 1920, 80), orderColours[0]), 0);     // nothing at the foot
        // Slide 2: the gold picture in the lower right.
        const QImage notices(result.slides[1]);
        QVERIFY(near(notices.pixelColor(1152 + 240, 648 + 135), QColor(0xFF, 0xD7, 0x00)));

        const DeckImporter::Result classic =
            DeckImporter::import(decks + QStringLiteral("/classic-4x3.pptx"), work.filePath(QStringLiteral("classic")), options);
        QVERIFY2(classic.ok(), qPrintable(classic.error));
        checkOrder(classic.slides, classicColours, QSize(1440, 1080));
    }

    void pptxThroughLibreOffice()
    {
        if (DeckImporter::findLibreOffice().isEmpty())
            QSKIP("LibreOffice is not installed");
        QTemporaryDir work;
        const DeckImporter::Result result = DeckImporter::import(decks + QStringLiteral("/order-16x9.pptx"), work.path());
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.method, QStringLiteral("libreoffice"));
        QVERIFY2(result.note.isEmpty(), qPrintable(result.note));
        checkOrder(result.slides, orderColours, QSize(1920, 1080));

        const DeckImporter::Result classic =
            DeckImporter::import(decks + QStringLiteral("/classic-4x3.pptx"), work.filePath(QStringLiteral("classic")));
        QVERIFY2(classic.ok(), qPrintable(classic.error));
        QCOMPARE(classic.method, QStringLiteral("libreoffice"));
        checkOrder(classic.slides, classicColours, QSize(1440, 1080));
    }

    void withoutOfficeSaysSo()
    {
        qputenv("LUMEN_SOFFICE", "none");
        QTemporaryDir work;
        const DeckImporter::Result result = DeckImporter::import(decks + QStringLiteral("/classic-4x3.pptx"), work.path());
        qunsetenv("LUMEN_SOFFICE");
        QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.method, QStringLiteral("basic"));
        QVERIFY2(result.note.contains(QStringLiteral("LibreOffice")), qPrintable(result.note));
    }

    void libraryStoresDecksAndReportsProblems()
    {
        auto db = core::data::Database::open(core::data::Database::inMemory());
        QVERIFY(db.ok());
        QTemporaryDir work;
        const QString pdf = makePdf(work.path(), {Qt::red, Qt::green}, -1);
        QFile bad(work.filePath(QStringLiteral("broken.pptx")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("this is not a zip file");
        bad.close();
        QFile keynote(work.filePath(QStringLiteral("talk.key")));
        QVERIFY(keynote.open(QIODevice::WriteOnly));
        keynote.write("keynote");
        keynote.close();

        PresentationLibrary library(*db.value(), work.filePath(QStringLiteral("slides")));
        DeckImporter::Options options;
        options.office = DeckImporter::Office::Never;
        const DeckImportReport first = library.importFiles(
            {decks + QStringLiteral("/order-16x9.pptx"), decks + QStringLiteral("/classic-4x3.pptx"), pdf,
             work.filePath(QStringLiteral("broken.pptx")), work.filePath(QStringLiteral("talk.key")),
             work.filePath(QStringLiteral("missing.pdf"))},
            options);
        QCOMPARE(first.summary(), QStringLiteral("3 imported, 3 failed"));
        QVERIFY(first.messages.join(u'\n').contains(QStringLiteral("order-16x9.pptx: 5 slides, 1 hidden left out")));
        QVERIFY(first.messages.join(u'\n').contains(QStringLiteral("Export To > PDF")));

        const DeckImportReport again = library.importFiles({decks + QStringLiteral("/classic-4x3.pptx")}, options);
        QCOMPARE(again.summary(), QStringLiteral("0 imported, 1 skipped"));

        auto rows = library.list();
        QVERIFY(rows.ok());
        QCOMPARE(int(rows.value().size()), 3);
        QMap<QString, int> counts;
        for (const DeckRow &row : rows.value()) {
            counts.insert(row.title, row.slides);
            QVERIFY(QFileInfo::exists(row.thumbnail));
        }
        QCOMPARE(counts.value(QStringLiteral("order-16x9")), 5);
        QCOMPARE(counts.value(QStringLiteral("classic-4x3")), 3);
        QCOMPARE(counts.value(QStringLiteral("sermon")), 2);

        // The stored slides load in order, as pictures.
        QString orderId;
        for (const DeckRow &row : rows.value())
            orderId = row.title == QLatin1String("order-16x9") ? row.presentationId : orderId;
        auto view = loadPresentationView(*db.value(), orderId);
        QVERIFY(view.ok());
        QCOMPARE(int(view.value().slides.size()), 5);
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(view.value().slides[size_t(i)].mediaKind, QStringLiteral("image"));
            QVERIFY(near(QImage(view.value().slides[size_t(i)].mediaPath).pixelColor(8, 8), orderColours[i]));
        }
    }

    // The Presentations tab: import, click the card, send a slide live.
    void presentationsTabShowsDecks()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QVERIFY(ui.click(QStringLiteral("libraryTab-3")));
        QSignalSpy imported(ui.controller.get(), &OperatorController::decksImported);
        QVERIFY(ui.controller->importPresentations({QUrl::fromLocalFile(decks + QStringLiteral("/order-16x9.pptx"))}));
        QVERIFY(!ui.controller->importPresentations({})); // one import at a time
        QVERIFY(imported.wait(60000));
        QCOMPARE(imported.first().first().toString(), QStringLiteral("1 imported"));
        ui.settle();
        QVERIFY(ui.item(QStringLiteral("deckCard-0")));
        QCOMPARE(ui.item(QStringLiteral("deckCard-0"))->property("badge").toString(), QStringLiteral("5 slides"));
        QVERIFY(ui.item(QStringLiteral("deckSummary"))->isVisible());

        QVERIFY(ui.click(QStringLiteral("deckCard-0")));
        QCOMPARE(ui.controller->selectedDeck(), 0);
        QCOMPARE(ui.controller->itemTitle(), QStringLiteral("order-16x9"));
        auto *slides = qobject_cast<RowModel *>(ui.controller->slides());
        QCOMPARE(slides->rowCount(), 5);

        QVERIFY(ui.controller->goLive(2));
        const core::show::Content &live = ui.show.scene().layer(core::show::Layer::Media).content;
        QCOMPARE(live.kind, core::show::Content::Kind::Image);
        QVERIFY(near(QImage(live.imagePath).pixelColor(8, 8), orderColours[2]));
        QVERIFY(ui.show.scene().layer(core::show::Layer::Slide).content.isEmpty());

        QVERIFY(ui.click(QStringLiteral("deckToPlan")));
        auto *plan = qobject_cast<RowModel *>(ui.controller->plan());
        QCOMPARE(plan->get(plan->rowCount() - 1).value(QStringLiteral("title")).toString(), QStringLiteral("order-16x9"));
    }
};

QTEST_MAIN(TestPresentations)
#include "tst_presentations.moc"
