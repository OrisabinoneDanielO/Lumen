#pragma once

#include <QImage>
#include <QLibrary>
#include <QObject>
#include <QTimer>

namespace lumen::render {

// NDI output (batch 15, experimental). Sends the stream output's frames, with
// their alpha, as an NDI source on the local network, using the NDI Runtime
// the user has installed (from ndi.video). Nothing from the NDI SDK ships
// with Lumen: the runtime is loaded when this starts, and if it is not there
// the sink only reports that.
//
// NOT TESTED WITH A REAL NDI RUNTIME: no runtime was available when this was
// written, so it stays behind the experimental/ndi setting (off by default).
// Set LUMEN_NDI_LIBRARY to the runtime's library file to use a particular
// one.
class NdiSink : public QObject
{
    Q_OBJECT

public:
    explicit NdiSink(const QString &sourceName, int fps = 30, QObject *parent = nullptr);
    ~NdiSink() override;

    bool isSending() const { return m_sender != nullptr; }
    // "sending as <name>", or why not (for status and the operator).
    QString status() const { return m_status; }

    // The latest stream frame; it is sent at the frame rate until the next.
    void frame(const QImage &premultiplied);

private:
    void send();

    struct Api;
    std::unique_ptr<Api> m_api;
    QLibrary m_library;
    void *m_sender = nullptr;
    QByteArray m_name;
    QImage m_frame; // straight (not premultiplied) BGRA, as NDI takes it
    QTimer m_timer;
    int m_fps;
    QString m_status;
};

} // namespace lumen::render
