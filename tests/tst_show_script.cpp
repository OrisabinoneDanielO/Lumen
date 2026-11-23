#include "rendercheck.h"

#include "lumen/core/render/renderlink.h"
#include "lumen/core/show/outputbridge.h"
#include "lumen/core/show/showcontroller.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace lumen::core::render;
using namespace lumen::core::show;
using namespace lumen::test;

// Batch 4 "Done when": scripted commands produce the expected frames.
//
// Each tests/scripts/*.script file is played against a real lumen-render
// process through the show controller, exactly as the operator UI drives it.
// The renderer runs on a manual clock, so "advance 500" lands mid-fade
// precisely. Frames are checked by probing pixels, not by comparing to saved
// images, because fonts render slightly differently on each OS.
//
// Commands (one per line, # starts a comment):
//   set <layer> colour <#RRGGBB>       set <layer> text "<text>"
//   clear <layer> | clear all          transition <layer|overrides> cut | fade <ms>
//   logo on|off | logo text "<text>"   black on|off
//   advance <ms>
//   expect colour <#RRGGBB> at <x> <y>          (x, y: 0..1 of the frame)
//   expect mix <#a> <#b> <fraction> at <x> <y>  (a crossfade part-way)
//   expect [no] text in <top|middle|bottom|anywhere>     (full-white text)
//   expect [no] visible in <top|middle|bottom|anywhere>  (anything bright)
//   expect black                                  (nothing lit at all)
class ScriptRunner
{
public:
    ScriptRunner(ShowController &show, RenderLink &link, QString grabDir)
        : m_show(show), m_link(link), m_grabDir(std::move(grabDir))
    {}

    // Returns an empty string on success, else "file:line: what went wrong".
    QString run(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return path + QStringLiteral(": cannot read");
        if (auto manual = m_link.request({{"cmd", "clock"}, {"mode", "manual"}}); !manual.ok())
            return path + QStringLiteral(": ") + manual.error().message;

        int lineNumber = 0;
        int steps = 0;
        while (!file.atEnd()) {
            ++lineNumber;
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(u'#'))
                continue;
            const QString error = step(QProcess::splitCommand(line));
            if (!error.isEmpty()) {
                return QStringLiteral("%1:%2: %3\n    %4")
                    .arg(QFileInfo(path).fileName())
                    .arg(lineNumber)
                    .arg(error, line);
            }
            ++steps;
        }
        return steps > 0 ? QString() : path + QStringLiteral(": no commands");
    }

private:
    static std::optional<std::pair<double, double>> band(const QString &name)
    {
        if (name == QLatin1String("top"))
            return std::pair{0.0, 0.12};
        if (name == QLatin1String("middle"))
            return std::pair{0.25, 0.75};
        if (name == QLatin1String("bottom"))
            return std::pair{0.75, 0.95};
        if (name == QLatin1String("anywhere"))
            return std::pair{0.0, 1.0};
        return std::nullopt;
    }

    static std::optional<double> fraction(const QString &text)
    {
        bool ok = false;
        const double value = text.toDouble(&ok);
        return ok && value >= 0.0 && value <= 1.0 ? std::optional(value) : std::nullopt;
    }

    // Waits until the renderer has handled everything sent so far, then grabs.
    QImage grab()
    {
        const QString path = m_grabDir + QStringLiteral("/frame-%1.png").arg(++m_grabs);
        auto reply = m_link.request({{"cmd", "grab"}, {"path", path}}, 5000);
        return reply.ok() ? QImage(path) : QImage();
    }

    QString step(const QStringList &w)
    {
        if (w.isEmpty())
            return {};
        const QString &verb = w[0];

        if (verb == QLatin1String("set") && w.size() == 4) {
            const auto layer = layerFromName(w[1]);
            if (!layer)
                return QStringLiteral("unknown layer");
            if (w[2] == QLatin1String("colour")) {
                const QColor colour = QColor::fromString(w[3]);
                if (!colour.isValid())
                    return QStringLiteral("bad colour");
                m_show.setContent(*layer, Content::fill(colour));
                return {};
            }
            if (w[2] == QLatin1String("text")) {
                m_show.setContent(*layer, Content::textContent(w[3]));
                return {};
            }
            return QStringLiteral("set needs colour or text");
        }

        if (verb == QLatin1String("clear") && w.size() == 2) {
            if (w[1] == QLatin1String("all")) {
                m_show.clearAll();
                return {};
            }
            const auto layer = layerFromName(w[1]);
            if (!layer)
                return QStringLiteral("unknown layer");
            m_show.clear(*layer);
            return {};
        }

        if (verb == QLatin1String("transition") && w.size() >= 3) {
            Transition transition;
            if (w[2] == QLatin1String("cut") && w.size() == 3) {
                transition = Transition::cut();
            } else if (w[2] == QLatin1String("fade") && w.size() == 4) {
                transition = Transition::fade(w[3].toInt());
            } else {
                return QStringLiteral("transition needs cut or fade <ms>");
            }
            if (w[1] == QLatin1String("overrides")) {
                m_show.setOverrideTransition(transition);
                return {};
            }
            const auto layer = layerFromName(w[1]);
            if (!layer)
                return QStringLiteral("unknown layer");
            m_show.setTransition(*layer, transition);
            return {};
        }

        if ((verb == QLatin1String("logo") || verb == QLatin1String("black")) && w.size() >= 2) {
            const bool logo = verb == QLatin1String("logo");
            if (logo && w[1] == QLatin1String("text") && w.size() == 3) {
                m_show.setLogoContent(Content::textContent(w[2]));
                return {};
            }
            if (w[1] != QLatin1String("on") && w[1] != QLatin1String("off"))
                return QStringLiteral("needs on or off");
            const bool on = w[1] == QLatin1String("on");
            logo ? m_show.setLogo(on) : m_show.setBlack(on);
            return {};
        }

        if (verb == QLatin1String("advance") && w.size() == 2) {
            auto reply = m_link.request({{"cmd", "clock"}, {"advanceMs", w[1].toLongLong()}});
            return reply.ok() ? QString() : reply.error().message;
        }

        if (verb == QLatin1String("expect"))
            return expect(w.mid(1));

        return QStringLiteral("unknown command");
    }

    QString expect(const QStringList &w)
    {
        const QImage frame = grab();
        if (frame.isNull())
            return QStringLiteral("could not grab a frame");

        // expect colour <c> at <x> <y>
        if (w.size() == 5 && w[0] == QLatin1String("colour") && w[2] == QLatin1String("at")) {
            const QColor want = QColor::fromString(w[1]);
            const auto x = fraction(w[3]), y = fraction(w[4]);
            if (!want.isValid() || !x || !y)
                return QStringLiteral("bad expectation");
            const QColor got = colourAt(frame, *x, *y);
            return near(got, want, 3) ? QString()
                                      : QStringLiteral("expected %1, got %2").arg(want.name(), got.name());
        }

        // expect mix <a> <b> <fraction> at <x> <y>
        if (w.size() == 7 && w[0] == QLatin1String("mix") && w[4] == QLatin1String("at")) {
            const QColor a = QColor::fromString(w[1]), b = QColor::fromString(w[2]);
            const auto amount = fraction(w[3]), x = fraction(w[5]), y = fraction(w[6]);
            if (!a.isValid() || !b.isValid() || !amount || !x || !y)
                return QStringLiteral("bad expectation");
            const QColor want = mix(a, b, *amount);
            const QColor got = colourAt(frame, *x, *y);
            return near(got, want, 5) ? QString()
                                      : QStringLiteral("expected %1, got %2").arg(want.name(), got.name());
        }

        if (w.size() == 1 && w[0] == QLatin1String("black")) {
            const int lit = pixelsBrighterThan(frame, 2);
            return lit == 0 ? QString() : QStringLiteral("%1 pixels are lit").arg(lit);
        }

        // expect [no] text|visible in <band>
        const bool negated = !w.isEmpty() && w[0] == QLatin1String("no");
        const QStringList rest = negated ? w.mid(1) : w;
        if (rest.size() == 3 && rest[1] == QLatin1String("in")
            && (rest[0] == QLatin1String("text") || rest[0] == QLatin1String("visible"))) {
            const auto area = band(rest[2]);
            if (!area)
                return QStringLiteral("unknown area");
            const QImage part = frame.copy(0, int(area->first * frame.height()), frame.width(),
                                           qMax(1, int((area->second - area->first) * frame.height())));
            const int count = rest[0] == QLatin1String("text") ? textPixels(part)
                                                               : pixelsBrighterThan(part, 100);
            const bool present = count > 20;
            if (present == !negated)
                return {};
            return QStringLiteral("%1 matching pixels").arg(count);
        }

        return QStringLiteral("unknown expectation");
    }

    ShowController &m_show;
    RenderLink &m_link;
    QString m_grabDir;
    int m_grabs = 0;
};

class TestShowScript : public QObject
{
    Q_OBJECT

private slots:
    void scripts_data()
    {
        QTest::addColumn<QString>("path");
        const QDir dir(QStringLiteral(LUMEN_SCRIPTS_DIR));
        const QStringList files = dir.entryList({QStringLiteral("*.script")}, QDir::Files, QDir::Name);
        QVERIFY2(files.size() >= 4, "the batch 4 scripts are missing");
        for (const QString &file : files)
            QTest::newRow(qPrintable(file)) << dir.filePath(file);
    }

    void scripts()
    {
        QFETCH(QString, path);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        RenderLink::Options options;
        options.serverName = QStringLiteral("lumen-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
        options.executable = QStringLiteral(LUMEN_RENDER_PATH);
        options.arguments = {QStringLiteral("--test-mode"), QStringLiteral("--windowed")};
        RenderLink link(options);
        auto started = link.start();
        QVERIFY2(started.ok(), started.ok() ? "" : qPrintable(started.error().message));

        ShowController show;
        OutputBridge bridge(show, link);
        QStringList sendErrors;
        connect(&bridge, &OutputBridge::sendFailed, this,
                [&sendErrors](const QString &error) { sendErrors << error; });
        auto attached = bridge.attach();
        QVERIFY(attached.ok());
        QCOMPARE(attached.value().sequence, quint64(0)); // a fresh renderer

        ScriptRunner runner(show, link, dir.path());
        const QString failure = runner.run(path);
        (void)link.quit();
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY2(sendErrors.isEmpty(), qPrintable(sendErrors.join(u'\n')));
    }
};

QTEST_GUILESS_MAIN(TestShowScript)
#include "tst_show_script.moc"
