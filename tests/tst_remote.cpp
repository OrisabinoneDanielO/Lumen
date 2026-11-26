#include "operatorharness.h"

#include "livecontrol.h"
#include "midiinput.h"
#include "oscserver.h"
#include "remoteserver.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QtEndian>
#include <QUdpSocket>

#ifdef LUMEN_HAVE_MIDI
#include <RtMidi.h>
#endif

using namespace lumen;
using namespace lumen::app;
using namespace lumen::core::show;

// Batch 19 "Done when": a remote and a MIDI controller drive slides on the
// LAN. The remote is the built-in web page's API over HTTP; the MIDI
// controller is a virtual MIDI device (no hardware here); OSC too.
namespace {

struct Http
{
    int status = 0;
    QByteArray body;
    QJsonObject json() const { return QJsonDocument::fromJson(body).object(); }
};

Http call(const QString &url, const QByteArray &method = "GET", const QString &code = {}, const QJsonObject &body = {})
{
    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(url)};
    if (!code.isEmpty())
        request.setRawHeader("X-Lumen-Code", code.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = method == "POST" ? network.post(request, QJsonDocument(body).toJson()) : network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    Http out{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll()};
    reply->deleteLater();
    return out;
}

QString liveText(test::OperatorHarness &ui)
{
    return ui.show.scene().layer(Layer::Slide).content.text;
}

} // namespace

class TestRemote : public QObject
{
    Q_OBJECT

private slots:
    // Done when (1): the phone remote drives the slides.
    void remoteDrivesSlides()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        LiveControl control(*ui.controller);
        RemoteServer remote(control, 0);
        QCOMPARE(remote.addresses().size(), 0); // off
        remote.setEnabled(true);
        QVERIFY(remote.enabled());
        QCOMPARE(remote.code().size(), 6);
        QVERIFY(!remote.addresses().isEmpty());
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(remote.port());

        // The page, for any phone browser, with no internet.
        const Http page = call(base + QStringLiteral("/"));
        QCOMPARE(page.status, 200);
        QVERIFY(page.body.contains("<title>Lumen Remote</title>"));
        QVERIFY(!page.body.contains("http://") && !page.body.contains("https://")); // nothing from the internet

        // No code, no state.
        QCOMPARE(call(base + QStringLiteral("/api/state")).status, 401);
        const Http state = call(base + QStringLiteral("/api/state"), "GET", remote.code());
        QCOMPARE(state.status, 200);
        const QJsonObject s = state.json().value("state").toObject();
        QCOMPARE(s.value("plan").toArray().size(), qobject_cast<RowModel *>(ui.controller->plan())->rowCount());

        // Pick Amazing Grace, then Next, Next, a slide, Black, Clear.
        QVERIFY(call(base + QStringLiteral("/api/item"), "POST", remote.code(), {{"index", 2}}).json().value("ok").toBool());
        QCOMPARE(ui.controller->itemTitle(), QStringLiteral("Amazing Grace"));
        const QString first = ui.controller->previewText();
        Http next = call(base + QStringLiteral("/api/next"), "POST", remote.code());
        QVERIFY(next.json().value("ok").toBool());
        QCOMPARE(liveText(ui), first);
        QCOMPARE(next.json().value("state").toObject().value("liveSlide").toInt(), 0);
        const QString second = ui.controller->previewText();
        call(base + QStringLiteral("/api/next"), "POST", remote.code());
        QCOMPARE(liveText(ui), second);
        call(base + QStringLiteral("/api/previous"), "POST", remote.code());
        QCOMPARE(liveText(ui), first);
        call(base + QStringLiteral("/api/slide"), "POST", remote.code(), {{"index", 3}});
        QCOMPARE(ui.controller->liveSlide(), 3);
        call(base + QStringLiteral("/api/black"), "POST", remote.code());
        QVERIFY(ui.controller->blackOn());
        call(base + QStringLiteral("/api/black"), "POST", remote.code());
        call(base + QStringLiteral("/api/clear"), "POST", remote.code(), {{"layer", "slide"}});
        QVERIFY(liveText(ui).isEmpty());
        QCOMPARE(call(base + QStringLiteral("/api/explode"), "POST", remote.code()).status, 404);
        QCOMPARE(call(base + QStringLiteral("/api/next"), "GET", remote.code()).status, 405);

        // In Edit mode the remote cannot send anything live.
        ui.controller->setMode(QStringLiteral("edit"));
        QVERIFY(!call(base + QStringLiteral("/api/next"), "POST", remote.code()).json().value("ok").toBool());
        ui.controller->setMode(QStringLiteral("show"));

        // Wrong codes: five, then a minute's wait even with the right one.
        for (int i = 0; i < 5; ++i)
            QCOMPARE(call(base + QStringLiteral("/api/state"), "GET", QStringLiteral("x")).status, 401);
        QCOMPARE(call(base + QStringLiteral("/api/state"), "GET", remote.code()).status, 429);

        remote.setEnabled(false);
        QVERIFY(remote.code().isEmpty());
    }

    // The mapping logic, without hardware: notes, CCs, program changes and
    // learning.
    void midiMessages()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        LiveControl control(*ui.controller);
        MidiInput midi(control);
        QCOMPARE(MidiInput::describe("note", 60), QStringLiteral("Note C4"));

        midi.handle({0xC0, 2}); // program change 3 (2 on the wire): the third item
        QCOMPARE(ui.controller->currentItem(), 2);
        midi.handle({0x90, 60, 100}); // C4: next
        QCOMPARE(ui.controller->liveSlide(), 0);
        midi.handle({0x80, 60, 0});   // its note off does nothing
        midi.handle({0x90, 60, 0});   // nor a note on with no velocity
        QCOMPARE(ui.controller->liveSlide(), 0);
        midi.handle({0x99, 60, 90});  // any channel
        QCOMPARE(ui.controller->liveSlide(), 1);
        midi.handle({0x90, 59, 90});  // B3: previous
        QCOMPARE(ui.controller->liveSlide(), 0);

        // Learn: a foot pedal (CC 64) for Black.
        QSignalSpy saved(&midi, &MidiInput::mappingsChanged);
        midi.learn(QStringLiteral("black"));
        midi.handle({0xB0, 64, 127});
        QVERIFY(midi.learning().isEmpty());
        QCOMPARE(saved.size(), 1);
        QVERIFY(!ui.controller->blackOn()); // learning did not act
        midi.handle({0xB0, 64, 0});
        midi.handle({0xB0, 64, 127});
        QVERIFY(ui.controller->blackOn());
        midi.handle({0xB0, 64, 127}); // held: no second press
        QVERIFY(ui.controller->blackOn());
        midi.handle({0x90, 67, 100}); // G4 was Black: learning moved Black to the pedal
        QVERIFY(ui.controller->blackOn());
        midi.handle({0xB0, 64, 0});
        midi.handle({0xB0, 64, 127}); // the pedal again: Black off
        QVERIFY(!ui.controller->blackOn());

        // Saved and restored.
        MidiInput restored(control);
        restored.setMappingsJson(midi.mappingsJson());
        QCOMPARE(restored.mappingsJson(), midi.mappingsJson());
    }

    // Done when (2): a MIDI controller (a virtual one) drives the slides.
    void midiControllerDrivesSlides()
    {
#ifndef LUMEN_HAVE_MIDI
        QSKIP("built without MIDI");
#else
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        LiveControl control(*ui.controller);
        std::unique_ptr<RtMidiOut> pads;
        try {
            pads = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "Test Pads");
            pads->openVirtualPort("Test Pads out");
        } catch (const RtMidiError &e) {
            QSKIP(qPrintable(QStringLiteral("no MIDI system here: ") + QString::fromStdString(e.getMessage())));
        }
        MidiInput midi(control);
        midi.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(midi.devices().join(u' ').contains(QLatin1String("Test Pads")), 8000);

        ui.controller->selectItem(2);
        std::vector<unsigned char> press{0x90, 60, 100}, release{0x80, 60, 0};
        pads->sendMessage(&press);
        pads->sendMessage(&release);
        QTRY_COMPARE(ui.controller->liveSlide(), 0);
        pads->sendMessage(&press);
        QTRY_COMPARE(ui.controller->liveSlide(), 1);
        std::vector<unsigned char> item{0xC0, 4};
        pads->sendMessage(&item);
        QTRY_COMPARE(ui.controller->currentItem(), 4);
        QVERIFY(midi.last().startsWith(QStringLiteral("Program change")));

        // Unplugged: it goes from the list.
        pads.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!midi.devices().join(u' ').contains(QLatin1String("Test Pads")), 8000);
#endif
    }

    // The Remote control popover from the Plan menu.
    void remotePopover()
    {
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        QVERIFY(ui.click("planMenuButton"));
        QVERIFY(ui.click("planRemote"));
        ui.settle();
        QVERIFY(ui.item("remotePopover") && ui.item("remoteToggle"));
        QVERIFY(ui.click("remoteToggle"));
        QVERIFY(ui.remote->enabled());
        QTRY_COMPARE(ui.item("remoteCode")->property("text").toString(), ui.remote->code());
        QVERIFY(ui.item("remoteAddress")->property("text").toString().startsWith(QStringLiteral("http://")));
        QVERIFY(ui.click("oscToggle"));
        QVERIFY(ui.osc->enabled());
        if (MidiInput::available()) {
            QVERIFY(ui.click("midiToggle"));
            QVERIFY(ui.midi->enabled());
            ui.settle();
            QVERIFY(ui.click("midiLearn-black"));
            QCOMPARE(ui.midi->learning(), QStringLiteral("black"));
            ui.midi->handle({0x90, 72, 100});
            QVERIFY(ui.midi->learning().isEmpty());
        }
    }

    void oscMessages()
    {
        const QByteArray packet = OscServer::encode(QStringLiteral("/lumen/slide"), {3});
        QCOMPARE(packet.size() % 4, 0);
        const auto messages = OscServer::parse(packet);
        QCOMPARE(messages.size(), 1);
        QCOMPARE(messages[0].address, QStringLiteral("/lumen/slide"));
        QCOMPARE(messages[0].arguments.value(0).toInt(), 3);
        // A bundle of two.
        QByteArray bundle("#bundle\0", 8);
        bundle += QByteArray(8, '\0');
        for (const QByteArray &m : {OscServer::encode("/lumen/next"), OscServer::encode("/lumen/black", {1.0})}) {
            char size[4];
            qToBigEndian<qint32>(qint32(m.size()), size);
            bundle += QByteArray(size, 4) + m;
        }
        QCOMPARE(OscServer::parse(bundle).size(), 2);
        QVERIFY(OscServer::parse("not osc").isEmpty());

        // Over UDP: next, a button's release (ignored), black, slide 1.
        test::OperatorHarness ui;
        QString error;
        QVERIFY2(ui.load(true, &error), qPrintable(error));
        LiveControl control(*ui.controller);
        OscServer osc(control, 0);
        osc.setEnabled(true);
        QVERIFY(osc.enabled() && osc.port() > 0);
        ui.controller->selectItem(2);
        QUdpSocket out;
        out.writeDatagram(OscServer::encode("/lumen/next", {1.0}), QHostAddress::LocalHost, quint16(osc.port()));
        QTRY_COMPARE(ui.controller->liveSlide(), 0);
        out.writeDatagram(OscServer::encode("/lumen/next", {0.0}), QHostAddress::LocalHost, quint16(osc.port()));
        out.writeDatagram(OscServer::encode("/lumen/black"), QHostAddress::LocalHost, quint16(osc.port()));
        QTRY_VERIFY(ui.controller->blackOn());
        QCOMPARE(ui.controller->liveSlide(), 0); // the release did not move on
        out.writeDatagram(OscServer::encode("/lumen/black"), QHostAddress::LocalHost, quint16(osc.port()));
        out.writeDatagram(OscServer::encode("/lumen/slide", {4}), QHostAddress::LocalHost, quint16(osc.port()));
        QTRY_COMPARE(ui.controller->liveSlide(), 3);
        out.writeDatagram(OscServer::encode("/lumen/message", {QStringLiteral("Parent of 7")}), QHostAddress::LocalHost,
                          quint16(osc.port()));
        QTRY_COMPARE(ui.controller->messageShown(), QStringLiteral("Parent of 7"));
        out.writeDatagram(OscServer::encode("/lumen/clear/messages"), QHostAddress::LocalHost, quint16(osc.port()));
        QTRY_VERIFY(ui.controller->messageShown().isEmpty());
    }
};

QTEST_MAIN(TestRemote)
#include "tst_remote.moc"
