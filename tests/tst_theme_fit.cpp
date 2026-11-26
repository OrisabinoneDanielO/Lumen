#include "outputwindow.h"
#include "themes.h"

#include "lumen/core/show/textstyle.h"

#include <QGuiApplication>
#include <QJSValue>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRawFont>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QTextLayout>
#include <QtQuickTest/quicktest.h>

using namespace lumen;
using namespace lumen::core::show;
using lumen::render::OutputWindow;

// Batch 8 "Done when": long verses fit; diacritics render correctly.
//
// Long verses: the longest verse of every bundled Bible (Esther 8:9 in most)
// is shown with the Scripture theme on a real 1920x1080 output. Every slide
// must fit at or above the theme's smallest size (splitting across slides if
// needed), the text must not be cut off, and every lit pixel must lie inside
// the safe area.
//
// Diacritics: every character used by the Yoruba, Igbo and Hausa Bibles must
// have a real glyph in the output fonts; sample lines must shape without
// missing glyphs or fallback fonts; and stacked tone marks (ẹ́, ọ̀) must draw
// marks above and below the letter.
class TestThemeFit : public QObject
{
    Q_OBJECT

private:
    QSqlDatabase m_bibles;
    std::unique_ptr<OutputWindow> m_output;
    quint64 m_sequence = 0;

    static QQuickItem *find(QQuickItem *item, const QString &name)
    {
        if (!item)
            return nullptr;
        if (item->objectName() == name && item->isVisible())
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = find(child, name))
                return found;
        }
        return nullptr;
    }

    // Shows one styled slide on the scripture background and returns the frame.
    QImage show(const QString &text, const TextStyle &style, int size, const QString &footer,
                const QColor &background)
    {
        Scene scene;
        scene.sequence = ++m_sequence;
        scene.layer(Layer::Background).content = Content::fill(background);
        scene.layer(Layer::Slide).content = Content::styledText(text, app::styleWithSize(style, size), footer);
        m_output->setScene(scene);
        QQuickTest::qWaitForPolish(m_output->window());
        return m_output->grab();
    }

    // Rows that contain lit pixels (brighter than the background by far).
    static std::pair<int, int> inkRows(const QImage &frame)
    {
        int top = -1, bottom = -1;
        for (int y = 0; y < frame.height(); ++y) {
            for (int x = 0; x < frame.width(); ++x) {
                if (qGray(frame.pixel(x, y)) > 140) {
                    if (top < 0)
                        top = y;
                    bottom = y;
                    break;
                }
            }
        }
        return {top, bottom};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(registerOutputFonts().contains(QStringLiteral("Noto Sans")));
        QVERIFY(registerOutputFonts().contains(QStringLiteral("Noto Serif")));

        m_bibles = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("bibles"));
        m_bibles.setDatabaseName(QStringLiteral(LUMEN_BIBLES_DB));
        m_bibles.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        QVERIFY(m_bibles.open());

        m_output = std::make_unique<OutputWindow>();
        QVERIFY(m_output->create());
        m_output->place(QGuiApplication::primaryScreen(), false, QSize(1920, 1080));
        QVERIFY(QTest::qWaitForWindowExposed(m_output->window()));
        QTRY_COMPARE(m_output->window()->size(), QSize(1920, 1080));
    }

    void cleanupTestCase()
    {
        m_output.reset();
        m_bibles.close();
    }

    // ---- long verses fit ------------------------------------------------------

    void longestVerseFits_data()
    {
        QTest::addColumn<QString>("translation");
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("footer");
        QSqlQuery translations(m_bibles);
        QVERIFY(translations.exec("SELECT id, abbreviation, copyright FROM translations ORDER BY ordinal"));
        while (translations.next()) {
            QSqlQuery longest(m_bibles);
            longest.prepare("SELECT v.book, v.chapter, v.verse, v.text, b.name FROM verses v JOIN books b "
                            "ON b.translation = v.translation AND b.code = v.book WHERE v.translation = ? "
                            "ORDER BY length(v.text) DESC LIMIT 1");
            longest.addBindValue(translations.value(0));
            QVERIFY(longest.exec() && longest.next());
            const QString label = QStringLiteral("%1 %2:%3").arg(longest.value(4).toString())
                                      .arg(longest.value(1).toInt()).arg(longest.value(2).toInt());
            const QString footer = QStringLiteral("%1 (%2) · %3").arg(label, translations.value(1).toString(),
                                                                        translations.value(2).toString());
            QTest::newRow(qPrintable(translations.value(0).toString() + " " + label))
                << translations.value(0).toString() << label << longest.value(3).toString() << footer;
        }
    }

    void longestVerseFits()
    {
        QFETCH(QString, label);
        QFETCH(QString, text);
        QFETCH(QString, footer);
        QVERIFY(text.size() > 300); // these really are long

        const app::ThemeView theme = app::builtInTheme(core::data::ThemeTarget::Scripture);
        const TextStyle &style = theme.style;
        const QColor background = QColor::fromString(theme.background.value("colour").toString());
        const QStringList parts = splitToFit(text, style, true);
        QVERIFY(!parts.isEmpty());
        QCOMPARE(parts.join(u' ').simplified(), text.simplified()); // nothing lost
        qInfo("%s: %lld characters on %lld slide(s)", qPrintable(label), qlonglong(text.size()), qlonglong(parts.size()));

        const QRectF safe = textAreas(style, referenceSize, true).body.united(textAreas(style, referenceSize, true).footer);
        for (const QString &part : parts) {
            const Fit fit = fitText(part, style, true);
            QVERIFY2(fit.fits, qPrintable(label + ": a part does not fit even at the smallest size"));
            QVERIFY(fit.pixelSize >= style.minSize && fit.pixelSize <= style.maxSize);

            const QImage frame = show(part, style, fit.pixelSize, footer, background);
            QCOMPARE(frame.size(), QSize(1920, 1080));

            QQuickItem *body = find(m_output->window()->contentItem(), QStringLiteral("bodyText"));
            QVERIFY(body);
            QVERIFY2(!body->property("truncated").toBool(), qPrintable(label + ": text cut off"));
            QVERIFY(body->property("contentHeight").toDouble() <= body->height() + 1.0);
            const int drawnSize = body->property("fontInfo").value<QJSValue>().property("pixelSize").toInt();
            QVERIFY2(drawnSize >= style.minSize * 0.95,
                     qPrintable(QStringLiteral("%1: drawn at %2 px").arg(label).arg(drawnSize)));

            // Every lit pixel lies inside the safe area (allowing the shadow).
            const QRect allowed = safe.toAlignedRect().adjusted(-1, -1, 5, 5);
            int outside = 0;
            for (int y = 0; y < frame.height(); ++y) {
                for (int x = 0; x < frame.width(); ++x) {
                    if (qGray(frame.pixel(x, y)) > 110 && !allowed.contains(x, y))
                        ++outside;
                }
            }
            QVERIFY2(outside == 0, qPrintable(QStringLiteral("%1: %2 lit pixels outside the safe area").arg(label).arg(outside)));
        }
    }

    // A short verse stays big; the splitter keeps whole sentences together.
    void shortTextUsesTheLargestSize()
    {
        const TextStyle style = app::builtInTheme(core::data::ThemeTarget::Scripture).style;
        const Fit fit = fitText(QStringLiteral("Jesus wept."), style, true);
        QVERIFY(fit.fits);
        QCOMPARE(fit.pixelSize, style.maxSize);
    }

    // ---- diacritics render correctly ---------------------------------------------

    void everyAfricanBibleCharacterHasAGlyph()
    {
        QSet<char32_t> characters;
        QSqlQuery verses(m_bibles);
        QVERIFY(verses.exec("SELECT text FROM verses WHERE translation IN ('yor', 'hausa', 'ibo')"));
        while (verses.next()) {
            for (const char32_t c : verses.value(0).toString().toUcs4())
                characters.insert(c);
        }
        QVERIFY(characters.size() > 60);

        for (const QString &family : {QStringLiteral("Noto Sans"), QStringLiteral("Noto Serif")}) {
            const QRawFont raw = QRawFont::fromFont(QFont(family));
            QCOMPARE(raw.familyName(), family);
            QStringList missing;
            for (const char32_t c : characters) {
                if (QChar::isSpace(c) || QChar::category(c) == QChar::Other_Control || c == 0xFEFF)
                    continue;
                if (!raw.supportsCharacter(c))
                    missing << QStringLiteral("U+%1").arg(quint32(c), 4, 16, QLatin1Char('0')).toUpper();
            }
            QVERIFY2(missing.isEmpty(), qPrintable(family + " lacks: " + missing.join(u' ')));
        }
    }

    void sampleLinesShapeWithoutMissingGlyphs_data()
    {
        QTest::addColumn<QString>("line");
        QTest::newRow("Yoruba") << QStringLiteral("Ẹ jẹ́ ká yin Olúwa, Ọba ògo, ṣé àánú rẹ̀ dúró láéláé.");
        QTest::newRow("Yoruba decomposed") << QStringLiteral("Ẹ jẹ́ ká yin Ọba ògo");
        QTest::newRow("Igbo") << QStringLiteral("Ọ dị mma, Chineke m, ị dị ukwuu; ṅụọ mmiri, Ụ̀mụ̀.");
        QTest::newRow("Hausa") << QStringLiteral("Ɗaukaka ga Allah, ƙauna ta ƴanci; Ɓarna ba za ta ɓata ba. ʼYa'ya");
    }
    void sampleLinesShapeWithoutMissingGlyphs()
    {
        QFETCH(QString, line);
        for (const QString &family : {QStringLiteral("Noto Sans"), QStringLiteral("Noto Serif")}) {
            QFont font(family);
            font.setPixelSize(64);
            QTextLayout layout(line, font);
            layout.beginLayout();
            layout.createLine();
            layout.endLayout();
            const auto runs = layout.glyphRuns();
            QVERIFY(!runs.isEmpty());
            for (const QGlyphRun &run : runs) {
                QCOMPARE(run.rawFont().familyName(), family); // no fallback font
                for (const quint32 glyph : run.glyphIndexes())
                    QVERIFY2(glyph != 0, qPrintable(family + ": missing glyph in " + line));
            }
        }
    }

    void stackedToneMarksDrawAboveAndBelow_data()
    {
        QTest::addColumn<QString>("plain");
        QTest::addColumn<QString>("marked");
        QTest::newRow("e / ẹ́") << QStringLiteral("e") << QStringLiteral("ẹ́");
        QTest::newRow("o / ọ̀") << QStringLiteral("o") << QStringLiteral("ọ̀");
        QTest::newRow("u / ụ̀ (Igbo)") << QStringLiteral("u") << QStringLiteral("ụ̀");
    }
    void stackedToneMarksDrawAboveAndBelow()
    {
        QFETCH(QString, plain);
        QFETCH(QString, marked);
        TextStyle style;
        style.maxSize = style.minSize = 400;
        style.shadow = false;
        const auto [plainTop, plainBottom] = inkRows(show(plain, style, 400, {}, Qt::black));
        const auto [markedTop, markedBottom] = inkRows(show(marked, style, 400, {}, Qt::black));
        QVERIFY(plainTop >= 0 && markedTop >= 0);
        const int height = plainBottom - plainTop;
        QVERIFY2(markedTop < plainTop - height / 5,
                 qPrintable(QStringLiteral("no mark above: %1 vs %2").arg(markedTop).arg(plainTop)));
        QVERIFY2(markedBottom > plainBottom + height / 10,
                 qPrintable(QStringLiteral("no mark below: %1 vs %2").arg(markedBottom).arg(plainBottom)));
    }
};

QTEST_MAIN(TestThemeFit)
#include "tst_theme_fit.moc"
