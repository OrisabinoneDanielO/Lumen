#include "operatorharness.h"
#include "servicedata.h"
#include "themecontroller.h"
#include "themes.h"

#include "lumen/core/data/repository.h"
#include "lumen/core/show/textstyle.h"

#include <QRegularExpression>
#include <QTest>

using namespace lumen;
using namespace lumen::core;
using namespace lumen::core::data;
using namespace lumen::core::show;
using lumen::test::OperatorHarness;

// Themes (batch 8): defaults, resolution, footers, fitting, going live, and the
// theme editor.
class TestThemes : public QObject
{
    Q_OBJECT

private:
    static std::unique_ptr<Database> emptyLibrary()
    {
        return std::move(Database::open(Database::inMemory())).value();
    }

    static int countThemes(Database &db, ThemeTarget target, bool defaultsOnly)
    {
        int n = 0;
        const auto themes = Repository<Theme>(db).list();
        for (const Theme &t : themes.value()) {
            if (t.appliesTo == target && (!defaultsOnly || t.isDefault))
                ++n;
        }
        return n;
    }

private slots:
    void initTestCase() { QVERIFY(!registerOutputFonts().isEmpty()); }

    // ---- style and fitting ----------------------------------------------------

    void styleRoundTripsThroughJson()
    {
        TextStyle style;
        style.font = QStringLiteral("Noto Serif");
        style.weight = 400;
        style.maxSize = 80;
        style.minSize = 30;
        style.colour = QColor(0xFF, 0xE7, 0xA3);
        style.align = QStringLiteral("left");
        style.valign = QStringLiteral("bottom");
        style.outline = 4;
        style.shadow = false;
        style.safeArea = QRectF(0.1, 0.2, 0.8, 0.7);
        style.footerPosition = QStringLiteral("above");
        style.footerSize = 20;
        QCOMPARE(TextStyle::fromJson(style.toJson()), style);
        // Missing or bad fields keep defaults.
        QCOMPARE(TextStyle::fromJson({}), TextStyle{});
        QCOMPARE(TextStyle::fromJson({{"minSize", 999}}).minSize, TextStyle{}.maxSize);
        QCOMPARE(TextStyle::fromJson({{"safeArea", QJsonObject{{"x", 0.9}, {"y", 0}, {"w", 0.9}, {"h", 1}}}}).safeArea,
                 TextStyle{}.safeArea);
    }

    void footerAreas()
    {
        TextStyle style;
        const QSizeF size(1920, 1080);
        const TextAreas none = textAreas(style, size, false);
        QCOMPARE(none.body, QRectF(96, 54, 1728, 972));
        QVERIFY(none.footer.isEmpty());

        const TextAreas below = textAreas(style, size, true);
        QVERIFY(below.footer.top() > below.body.bottom());
        QCOMPARE(below.footer.bottom(), 1026.0);

        style.footerPosition = QStringLiteral("above");
        const TextAreas above = textAreas(style, size, true);
        QVERIFY(above.footer.bottom() < above.body.top());
        QCOMPARE(above.footer.top(), 54.0);

        style.footerPosition = QStringLiteral("none");
        QVERIFY(textAreas(style, size, true).footer.isEmpty());
    }

    void fittingShrinksThenSplits()
    {
        TextStyle style;
        const QString shortText = QStringLiteral("Amazing grace! How sweet the sound");
        QCOMPARE(fitText(shortText, style, false).pixelSize, style.maxSize);

        const QString longer = QString(shortText + QStringLiteral(". ")).repeated(16);
        const Fit shrunk = fitText(longer, style, false);
        QVERIFY(shrunk.fits);
        QVERIFY(shrunk.pixelSize < style.maxSize && shrunk.pixelSize >= style.minSize);

        const QString tooLong = QString(shortText + QStringLiteral(". ")).repeated(60);
        QVERIFY(!fitText(tooLong, style, false).fits);
        const QStringList parts = splitToFit(tooLong, style, false);
        QVERIFY(parts.size() > 1);
        for (const QString &part : parts) {
            QVERIFY(fitText(part, style, false).fits);
            QVERIFY(part.endsWith(u'.')); // split at sentence ends
        }
        QCOMPARE(parts.join(u' ').simplified(), tooLong.simplified());
    }

    // ---- defaults and resolution ---------------------------------------------------

    void defaultThemesAreCreatedOnce()
    {
        auto db = emptyLibrary();
        QVERIFY(app::ensureDefaultThemes(*db).ok());
        for (ThemeTarget target : {ThemeTarget::Song, ThemeTarget::Scripture, ThemeTarget::LowerThird})
            QCOMPARE(countThemes(*db, target, true), 1);
        QVERIFY(app::ensureDefaultThemes(*db).ok()); // idempotent
        QCOMPARE(Repository<Theme>(*db).list().value().size(), std::size_t(3));

        const auto scripture = app::defaultTheme(*db, ThemeTarget::Scripture);
        QVERIFY(scripture);
        QCOMPARE(scripture->style.font, QStringLiteral("Noto Serif"));
        QCOMPARE(scripture->style, app::builtInTheme(ThemeTarget::Scripture).style);
    }

    void anExistingThemeIsAdopted()
    {
        auto db = emptyLibrary();
        const Theme mine = Repository<Theme>(*db).create({.name = "Our songs", .appliesTo = ThemeTarget::Song}).value();
        QVERIFY(app::ensureDefaultThemes(*db).ok());
        QCOMPARE(countThemes(*db, ThemeTarget::Song, false), 1);
        const auto adopted = app::defaultTheme(*db, ThemeTarget::Song);
        QCOMPARE(adopted->id, mine.id);
        QCOMPARE(adopted->name, QStringLiteral("Our songs"));
        QVERIFY(!adopted->background.isEmpty()); // given the built-in background
    }

    void themeForAPresentation()
    {
        auto db = emptyLibrary();
        QVERIFY(app::ensureDefaultThemes(*db).ok());
        const Library library = Repository<Library>(*db).create({.name = "L"}).value();
        app::ThemeView special = app::builtInTheme(ThemeTarget::Song);
        special.name = QStringLiteral("Christmas");
        special.style.colour = Qt::red;
        special = app::saveTheme(*db, special).value();

        auto make = [&](PresentationKind kind, const QString &themeId) {
            return Repository<Presentation>(*db).create(
                {.libraryId = library.id, .kind = kind, .title = "T", .themeId = themeId}).value();
        };
        QCOMPARE(app::themeFor(*db, make(PresentationKind::Song, special.id))->name, QStringLiteral("Christmas"));
        QCOMPARE(app::themeFor(*db, make(PresentationKind::Song, {}))->name, QStringLiteral("Lyrics, centred"));
        QCOMPARE(app::themeFor(*db, make(PresentationKind::Slides, {}))->name, QStringLiteral("Lyrics, centred"));
        QCOMPARE(app::themeFor(*db, make(PresentationKind::Scripture, {}))->name, QStringLiteral("Scripture"));
    }

    void makingADefaultReplacesTheOld()
    {
        auto db = emptyLibrary();
        QVERIFY(app::ensureDefaultThemes(*db).ok());
        app::ThemeView other = app::builtInTheme(ThemeTarget::Song);
        other.name = QStringLiteral("Evening");
        other.isDefault = true;
        QVERIFY(app::saveTheme(*db, other).ok());
        QCOMPARE(countThemes(*db, ThemeTarget::Song, true), 1);
        QCOMPARE(app::defaultTheme(*db, ThemeTarget::Song)->name, QStringLiteral("Evening"));
    }

    // ---- the operator side ------------------------------------------------------

    void songsGetFootersSizesAndBackgrounds()
    {
        OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        ui.controller->selectItem(2); // Amazing Grace
        QVERIFY(ui.controller->goLive(0));

        const Content &slide = ui.show.scene().layer(Layer::Slide).content;
        QVERIFY(!slide.style.isEmpty());
        QCOMPARE(slide.footer, QStringLiteral("Amazing Grace · John Newton · Public domain"));
        const int size = slide.style.value("size").toInt();
        QVERIFY(size >= slide.style.value("minSize").toInt() && size <= slide.style.value("maxSize").toInt());
        // The theme's background came with it, in the same change.
        QCOMPARE(ui.show.scene().layer(Layer::Background).content, Content::fill(QColor(0x14, 0x21, 0x3D)));
        QCOMPARE(ui.controller->liveFooter(), slide.footer);
        QCOMPARE(ui.controller->liveStyle().value("size").toInt(), size);
    }

    void editorSavesAndRestyles()
    {
        OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        app::ThemeController &themes = *ui.themes;
        QCOMPARE(themes.themes().size(), 3);

        int songTheme = -1;
        for (int i = 0; i < themes.themes().size(); ++i) {
            if (themes.themes()[i].toMap().value("appliesTo") == QLatin1String("song"))
                songTheme = i;
        }
        themes.select(songTheme);
        QVERIFY(!themes.dirty());
        themes.setStyleValue("bold", false);
        themes.setStyleValue("maxSize", 60);
        themes.setStyleValue("colour", QColor(0xFF, 0xE7, 0xA3));
        themes.setBackground(QColor(0x2E, 0x1F, 0x3A));
        themes.setStyleValue("margin", 0.1);
        QVERIFY(themes.dirty());
        QCOMPARE(themes.previewStyle().value("weight").toInt(), 400);
        QVERIFY(themes.previewStyle().value("size").toInt() <= 60);

        // Nothing changes for the show until Save.
        ui.controller->selectItem(2);
        QCOMPARE(ui.controller->previewStyle().value("weight").toInt(), 600);

        QVERIFY(themes.save());
        QVERIFY(!themes.dirty());
        QCOMPARE(ui.controller->previewStyle().value("weight").toInt(), 400);
        QCOMPARE(ui.controller->previewStyle().value("maxSize").toInt(), 60);
        QCOMPARE(ui.controller->previewBackground(), QColor(0x2E, 0x1F, 0x3A));
        QCOMPARE(app::defaultTheme(ui.database(), ThemeTarget::Song)->style.safeArea, QRectF(0.1, 0.1, 0.8, 0.8));

        // Revert throws away unsaved changes; Duplicate makes a new theme.
        themes.setStyleValue("align", "left");
        themes.revert();
        QCOMPARE(themes.style().value("align").toString(), QStringLiteral("center"));
        QVERIFY(themes.duplicate());
        QCOMPARE(themes.themes().size(), 4);
        QVERIFY(themes.name().endsWith(QLatin1String("(copy)")));
        QVERIFY(!themes.isDefault());
    }

    // Set LUMEN_SCREENSHOT_DIR to save the window with a themed slide live and
    // the theme editor open.
    void screenshots()
    {
        const QString dir = qEnvironmentVariable("LUMEN_SCREENSHOT_DIR");
        if (dir.isEmpty())
            QSKIP("LUMEN_SCREENSHOT_DIR not set");
        OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QVERIFY(ui.click("planItem-2"));
        QVERIFY(ui.click("slide-2"));
        ui.settle();
        QTest::qWait(100);
        QVERIFY(ui.window->grabWindow().save(dir + QStringLiteral("/themed-main.png")));
        QVERIFY(ui.click("libraryTab-4"));
        ui.settle();
        QTest::qWait(100);
        QVERIFY(ui.window->grabWindow().save(dir + QStringLiteral("/theme-editor.png")));
    }

    void editorInTheWindow()
    {
        OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(false, &error), qPrintable(error));
        QVERIFY(ui.click("libraryTab-4")); // Themes
        QQuickItem *dialog = ui.item("themeDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(ui.click("theme-0"));
        const bool wasBold = ui.themes->style().value("weight").toInt() >= 600;
        QVERIFY(ui.click("themeBold"));
        QCOMPARE(ui.themes->style().value("weight").toInt() >= 600, !wasBold);
        QVERIFY(ui.themes->dirty());
        QQuickItem *preview = ui.item("themePreview");
        QVERIFY(preview);
        QVERIFY(ui.click("saveTheme"));
        QVERIFY(!ui.themes->dirty());
        QTest::keyClick(ui.window, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->isVisible());

        // The Scripture tab opens quick search.
        QVERIFY(ui.click("libraryTab-1"));
        QTRY_VERIFY(ui.item("scriptureDialog") && ui.item("scriptureDialog")->isVisible());
    }
};

QTEST_MAIN(TestThemes)
#include "tst_themes.moc"
