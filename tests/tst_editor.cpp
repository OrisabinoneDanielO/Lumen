#include "operatorharness.h"
#include "outputwindow.h"
#include "rendercheck.h"

#include "lumen/core/data/repository.h"

#include <QUrl>

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::data;
using namespace lumen::core::show;

// Batch 16 "Done when": every element type can be added, styled and saved.
// Also: composed slides on the output and in Preview, the lyrics editor for
// songs, Cancel, slides, backgrounds, and that Edit mode never goes live.
namespace {

const QString picture = QStringLiteral(LUMEN_MEDIA_DIR "/bars-720p.png");
const QString video = QStringLiteral(LUMEN_MEDIA_DIR "/clip-360p-h264.mp4");

std::vector<Element> storedElements(Database &db, const QString &presentationId, int slideIndex = 0)
{
    std::vector<Slide> all;
    const auto groups = Repository<Group>(db).listChildren(presentationId);
    for (const Group &g : groups.value()) {
        const auto slides = Repository<Slide>(db).listChildren(g.id);
        all.insert(all.end(), slides.value().begin(), slides.value().end());
    }
    if (slideIndex >= int(all.size()))
        return {};
    auto elements = Repository<Element>(db).listChildren(all[size_t(slideIndex)].id).value();
    std::sort(elements.begin(), elements.end(), [](const Element &a, const Element &b) { return a.zOrder < b.zOrder; });
    return elements;
}

int slideCount(Database &db, const QString &presentationId)
{
    int count = 0;
    const auto groups = Repository<Group>(db).listChildren(presentationId);
    for (const Group &g : groups.value())
        count += int(Repository<Slide>(db).listChildren(g.id).value().size());
    return count;
}

} // namespace

class TestEditor : public QObject
{
    Q_OBJECT

private slots:
    void everyElementTypeAddedStyledAndSaved()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        EditorController &editor = *ui.editor;
        QVERIFY(editor.newPresentation(QStringLiteral("Announcements")));
        QVERIFY(editor.isOpen() && editor.canInsert());
        const QString presentationId = editor.presentationId();
        QCOMPARE(ui.controller->selectedPresentationId(), presentationId); // selected in the plan too

        // Add each type and style it.
        QVERIFY(editor.addElement("text"));
        editor.setText(QStringLiteral("Youth camp\nSign up by Sunday"));
        editor.setStyle("size", 96);
        editor.setStyle("colour", "#F2C38B");
        editor.setStyle("align", "left");
        editor.setFrame(editor.element(), 120, 80, 1200, 400);

        QVERIFY(editor.addElement("shape"));
        editor.setStyle("fill", "#5B8DEF");
        editor.setStyle("shape", "ellipse");

        QVERIFY(!editor.addElement("image")); // needs a file
        QVERIFY(editor.addElement("image", QUrl::fromLocalFile(picture)));
        editor.setStyle("fit", "cover");

        QVERIFY(editor.addElement("video", QUrl::fromLocalFile(video)));
        editor.setStyle("loop", false);

        QVERIFY(editor.addElement("clock"));
        editor.setStyle("format", "h:mm AP");

        QVERIFY(editor.addElement("timer"));
        editor.setTimerName(QStringLiteral("Sermon"));
        editor.setStyle("colour", "#E5484D");

        QVERIFY(editor.dirty());
        QVERIFY(editor.save());
        QVERIFY(!editor.dirty());

        // Stored, in order, with their styles.
        Database &db = ui.database();
        const auto elements = storedElements(db, presentationId);
        QCOMPARE(int(elements.size()), 6);
        QCOMPARE(elements[0].type, ElementType::Text);
        QCOMPARE(elements[0].content, QStringLiteral("Youth camp\nSign up by Sunday"));
        QCOMPARE(elements[0].style.value("size").toInt(), 96);
        QCOMPARE(elements[0].style.value("colour").toString(), QStringLiteral("#F2C38B"));
        QCOMPARE(elements[0].frame, QRectF(120, 80, 1200, 400));
        QCOMPARE(elements[1].type, ElementType::Shape);
        QCOMPARE(elements[1].style.value("shape").toString(), QStringLiteral("ellipse"));
        QCOMPARE(elements[2].type, ElementType::Image);
        QCOMPARE(elements[2].style.value("fit").toString(), QStringLiteral("cover"));
        QCOMPARE(Repository<MediaAsset>(db).get(elements[2].mediaAssetId).value().filePath, picture);
        QCOMPARE(elements[3].type, ElementType::Video);
        QCOMPARE(Repository<MediaAsset>(db).get(elements[3].mediaAssetId).value().filePath, video);
        QCOMPARE(elements[4].type, ElementType::Clock);
        QCOMPARE(elements[4].style.value("format").toString(), QStringLiteral("h:mm AP"));
        QCOMPARE(elements[5].type, ElementType::Timer);
        QCOMPARE(elements[5].content, QStringLiteral("Sermon"));

        // Reopened, the draft is the same.
        QVERIFY(editor.openPresentation(presentationId));
        QCOMPARE(editor.elements().size(), 6);
        QCOMPARE(editor.title(), QStringLiteral("Announcements"));

        // The new presentation is in the plan; sent live it is composed, with
        // the countdown's timer resolved for the renderer.
        ui.controller->setMode(QStringLiteral("show"));
        auto *plan = qobject_cast<RowModel *>(ui.controller->plan());
        ui.controller->selectItem(plan->rowCount() - 1);
        QCOMPARE(ui.controller->itemTitle(), QStringLiteral("Announcements"));
        QCOMPARE(plan->get(plan->rowCount() - 1).value("kind").toString(), QStringLiteral("SLIDES")); // not TIMER
        QCOMPARE(ui.controller->previewElements().size(), 6);
        QVERIFY(ui.controller->goLive(0));
        const Content live = ui.show.scene().layer(Layer::Slide).content;
        QCOMPARE(live.kind, Content::Kind::Composed);
        QCOMPARE(live.elements.size(), 6);
        QCOMPARE(live.elements[5].toObject().value("timer").toObject().value("mode").toString(), QStringLiteral("countdown"));
        QVERIFY(live.elements[2].toObject().value("image").toString().startsWith(QStringLiteral("file://")));
        QCOMPARE(ui.controller->liveElements().size(), 6);
    }

    void outputDrawsComposedSlides()
    {
        render::OutputWindow output;
        QVERIFY(output.create());
        output.place(QGuiApplication::primaryScreen(), false, QSize(960, 540));
        QVERIFY(QTest::qWaitForWindowExposed(output.window()));
        output.setManualClock(true);
        const qint64 wall = QDateTime(QDate::currentDate(), QTime(9, 41)).toMSecsSinceEpoch();
        output.setWallClock(wall);
        const QJsonArray elements{
            QJsonObject{{"type", "shape"}, {"frame", QJsonArray{0, 0, 960, 1080}}, {"style", QJsonObject{{"fill", "#5B8DEF"}}}},
            QJsonObject{{"type", "text"}, {"frame", QJsonArray{1000, 100, 800, 300}}, {"text", "Youth camp"},
                        {"style", QJsonObject{{"size", 96}, {"colour", "#FFFFFF"}}}},
            QJsonObject{{"type", "image"}, {"frame", QJsonArray{1000, 500, 400, 225}}, {"style", QJsonObject{{"fit", "cover"}}},
                        {"image", QUrl::fromLocalFile(picture).toString()}},
            QJsonObject{{"type", "clock"}, {"frame", QJsonArray{1500, 40, 380, 100}}, {"style", QJsonObject{{"format", "hh:mm"}}}},
            QJsonObject{{"type", "timer"}, {"frame", QJsonArray{1000, 800, 800, 200}}, {"style", QJsonObject{{"size", 120}}},
                        {"timer", QJsonObject{{"mode", "countdown"}, {"endsAtMs", wall + 90 * 1000}}}},
        };
        Scene scene;
        scene.sequence = 1;
        scene.layer(Layer::Slide).content = Content::composed(elements);
        output.setScene(scene);
        QImage frame;
        QTRY_VERIFY((frame = output.grab(), test::near(test::colourAt(frame, 0.25, 0.5), QColor(0x5B, 0x8D, 0xEF), 6)));
        const QVariantMap texts = output.visibleTexts("elementText-");
        QCOMPARE(texts.value("elementText-1").toString(), QStringLiteral("Youth camp"));
        QCOMPARE(texts.value("elementText-3").toString(), QStringLiteral("09:41"));
        QCOMPARE(texts.value("elementText-4").toString(), QStringLiteral("1:30"));
        // The picture is drawn (not the black output behind it); it loads
        // in the background.
        QTRY_VERIFY((frame = output.grab(), !test::near(test::colourAt(frame, 0.6, 0.6), Qt::black, 30)));
        output.advanceClock(31 * 1000);
        QCOMPARE(output.visibleTexts("elementText-").value("elementText-4").toString(), QStringLiteral("0:59"));
    }

    void slidesBackgroundTransitionAndCancel()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        EditorController &editor = *ui.editor;
        QVERIFY(editor.newPresentation(QStringLiteral("Notices")));
        const QString id = editor.presentationId();
        QVERIFY(editor.addElement("text"));
        editor.addSlide();
        QCOMPARE(editor.slide(), 1);
        QVERIFY(editor.setBackground(QUrl::fromLocalFile(picture)));
        QVERIFY(editor.addElement("text"));
        editor.setTransition("cut");
        editor.duplicateSlide();
        QCOMPARE(editor.slides()->property("count").toInt(), 3);
        QVERIFY(editor.save());
        Database &db = ui.database();
        QCOMPARE(slideCount(db, id), 3);
        const auto second = storedElements(db, id, 1);
        QCOMPARE(int(second.size()), 2);
        QCOMPARE(second[0].style.value("role").toString(), QStringLiteral("background")); // at the bottom
        QCOMPARE(second[0].frame, QRectF(0, 0, 1920, 1080));

        // Cancel throws changes away.
        editor.selectSlide(0);
        QVERIFY(editor.removeSlide());
        QVERIFY(editor.addElement("shape"));
        QVERIFY(editor.dirty());
        editor.cancel();
        QVERIFY(!editor.dirty());
        QCOMPARE(editor.slides()->property("count").toInt(), 3);
        QCOMPARE(slideCount(db, id), 3);

        // Arranging: forward, back, delete.
        editor.selectSlide(1);
        editor.selectElement(1);
        editor.lower();
        QCOMPARE(editor.element(), 0);
        QCOMPARE(editor.selected().value("type").toString(), QStringLiteral("text"));
        QVERIFY(editor.removeElement());
        QCOMPARE(editor.elements().size(), 1);
    }

    void songLyrics()
    {
        const QString lyrics = QStringLiteral("[Verse 1]\nAmazing grace! How sweet the sound\nThat saved a wretch like me!\n\n"
                                              "I once was lost, but now am found;\nWas blind, but now I see.\n\n"
                                              "[Chorus]\nMy chains are gone\nI've been set free");
        const QString text = EditorController::toSongText(lyrics);
        QVERIFY2(text.contains(QStringLiteral("me!\n---\nI once was lost")), qPrintable(text));

        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        ui.controller->selectItem(2); // Amazing Grace
        EditorController &editor = *ui.editor;
        QVERIFY(editor.openSelected());
        QVERIFY(editor.isSong());
        QVERIFY(!editor.canInsert());
        QVERIFY(!editor.addElement("text")); // songs are edited as lyrics
        QVERIFY(editor.lyrics().startsWith(QStringLiteral("[Verse 1]\nAmazing grace")));
        const QString id = ui.controller->selectedPresentationId();
        editor.setLyrics(lyrics);
        QVERIFY(editor.save());
        Database &db = ui.database();
        const auto groups = Repository<Group>(db).listChildren(id).value();
        QCOMPARE(int(groups.size()), 2);
        QCOMPARE(groups[0].label, QStringLiteral("Verse 1"));
        QCOMPARE(groups[1].label, QStringLiteral("Chorus"));
        QCOMPARE(slideCount(db, id), 3); // two slides of Verse 1, one of the chorus
        // The plan shows the new lyrics, with the rebuilt arrangement.
        QCOMPARE(ui.controller->itemTitle(), QStringLiteral("Amazing Grace"));
        QCOMPARE(ui.controller->slides()->property("count").toInt(), 3);
        QCOMPARE(ui.controller->arrangement().size(), 2);
        // Search finds the new words.
        auto hits = ui.songs->search(QStringLiteral("chains"));
        QVERIFY(hits.ok() && !hits.value().empty());
    }

    // Unsaved changes are never dropped without asking: choosing another item
    // asks Save / Discard / Keep editing; the editor follows library songs too.
    void unsavedChangesAreKept()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        ui.controller->selectItem(2); // Amazing Grace
        QVERIFY(ui.click("editMode"));
        QTRY_VERIFY(ui.item("editor")->isVisible());
        QTRY_COMPARE(ui.editor->title(), QStringLiteral("Amazing Grace"));
        const QString grace = ui.editor->presentationId();
        ui.editor->setTitle(QStringLiteral("Amazing Grace (edited)"));
        QVERIFY(ui.editor->dirty());

        ui.controller->selectItem(3); // another item, while the draft is dirty
        QTRY_VERIFY(ui.item("editorUnsaved")->isVisible());
        QCOMPARE(ui.editor->presentationId(), grace); // nothing dropped
        QVERIFY(ui.click("editorUnsavedKeep"));
        QTRY_VERIFY(!ui.item("editorUnsaved")->isVisible());
        QVERIFY(ui.editor->dirty());

        ui.controller->selectItem(4);
        QTRY_VERIFY(ui.item("editorUnsaved")->isVisible());
        QVERIFY(ui.click("editorUnsavedDiscard"));
        QTRY_VERIFY(!ui.editor->dirty());
        QCOMPARE(ui.editor->presentationId(), ui.controller->selectedPresentationId());
        QCOMPARE(Repository<Presentation>(ui.database()).get(grace).value().title, QStringLiteral("Amazing Grace"));

        // Save: kept, then the new item opens.
        ui.controller->selectItem(2);
        QTRY_COMPARE(ui.editor->presentationId(), grace);
        ui.editor->setTitle(QStringLiteral("Amazing Grace (saved)"));
        ui.controller->selectItem(3);
        QTRY_VERIFY(ui.item("editorUnsaved")->isVisible());
        QVERIFY(ui.click("editorUnsavedSave"));
        QTRY_COMPARE(ui.editor->presentationId(), ui.controller->selectedPresentationId());
        QCOMPARE(Repository<Presentation>(ui.database()).get(grace).value().title, QStringLiteral("Amazing Grace (saved)"));

        // Library songs one after another: the editor follows.
        QVERIFY(ui.controller->showSong(0));
        QTRY_COMPARE(ui.editor->presentationId(), ui.controller->selectedPresentationId());
        QVERIFY(ui.controller->showSong(1));
        QTRY_COMPARE(ui.editor->presentationId(), ui.controller->selectedPresentationId());
    }

    void editorWindow()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        const quint64 sequence = ui.show.scene().sequence;
        QVERIFY(ui.click("editMode"));
        QTRY_VERIFY(ui.item("editor")->isVisible());
        QVERIFY(ui.click("editorNew"));
        QTRY_VERIFY(ui.editor->canInsert());
        QVERIFY(ui.click("editorInsert-text"));
        QCOMPARE(ui.editor->elements().size(), 1);
        QVERIFY(ui.click("editorInsert-shape"));
        QVERIFY(ui.click("editorInsert-clock"));
        QVERIFY(ui.click("editorInsert-timer"));
        QCOMPARE(ui.editor->elements().size(), 4);

        // Select the text box on the canvas and drag it.
        ui.settle();
        QQuickItem *box = ui.item("editorElement-0");
        QVERIFY(box);
        // A point only the text box covers (x 300 of 1920; the others are
        // further right).
        const QPoint from = box->mapToScene(QPointF(box->width() * (300.0 - 192) / 1536, box->height() / 2)).toPoint();
        const QVariantMap before = ui.editor->selected();
        QTest::mousePress(ui.window, Qt::LeftButton, {}, from);
        for (int step = 1; step <= 5; ++step)
            QTest::mouseMove(ui.window, from + QPoint(step * 8, step * 4));
        QTest::mouseRelease(ui.window, Qt::LeftButton, {}, from + QPoint(40, 20));
        QCOMPARE(ui.editor->element(), 0);
        QVERIFY(ui.editor->selected().value("x").toDouble() > 192); // moved right of where it was added
        Q_UNUSED(before);

        // Style it from the panel.
        QVERIFY(ui.click("editorSizeUp"));
        QCOMPARE(ui.editor->selected().value("style").toMap().value("size").toInt(), 76);
        QVERIFY(ui.click("editorAlign-left"));
        QCOMPARE(ui.editor->selected().value("style").toMap().value("align").toString(), QStringLiteral("left"));
        QVERIFY(ui.click("editorSave"));
        QVERIFY2(!ui.editor->dirty(), qPrintable(ui.editor->message()));
        // LUMEN_EDITOR_DUMP=<file.png> keeps a picture of the editor.
        if (const QString dump = qEnvironmentVariable("LUMEN_EDITOR_DUMP"); !dump.isEmpty())
            ui.window->grabWindow().save(dump);
        // Nothing reached the screen from Edit mode.
        QCOMPARE(ui.show.scene().sequence, sequence);
    }
};

QTEST_MAIN(TestEditor)
#include "tst_editor.moc"
