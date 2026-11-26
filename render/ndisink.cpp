#include "ndisink.h"

#include <QDir>
#include <QFileInfo>

#include <cstdint>
#include <limits>

namespace lumen::render {

namespace {

// The few parts of the NDI C API that are used, declared here to match the
// NDI Runtime's exported functions (NDI SDK v5 and v6 share them).
struct NdiSendCreate
{
    const char *p_ndi_name;
    const char *p_groups;
    bool clock_video;
    bool clock_audio;
};

struct NdiVideoFrameV2
{
    int xres;
    int yres;
    std::uint32_t FourCC;
    int frame_rate_N;
    int frame_rate_D;
    float picture_aspect_ratio;
    int frame_format_type;   // 1: progressive
    std::int64_t timecode;   // INT64_MAX: let NDI make one
    std::uint8_t *p_data;
    int line_stride_in_bytes;
    const char *p_metadata;
    std::int64_t timestamp;
};

constexpr std::uint32_t fourCC(char a, char b, char c, char d)
{
    return std::uint32_t(std::uint8_t(a)) | std::uint32_t(std::uint8_t(b)) << 8 | std::uint32_t(std::uint8_t(c)) << 16
           | std::uint32_t(std::uint8_t(d)) << 24;
}

QStringList candidates()
{
    QStringList paths;
    if (const QString chosen = qEnvironmentVariable("LUMEN_NDI_LIBRARY"); !chosen.isEmpty())
        return {chosen};
#if defined(Q_OS_WIN)
    for (const char *variable : {"NDI_RUNTIME_DIR_V6", "NDI_RUNTIME_DIR_V5"}) {
        if (const QString dir = qEnvironmentVariable(variable); !dir.isEmpty())
            paths << QDir(dir).filePath(QStringLiteral("Processing.NDI.Lib.x64.dll"));
    }
    paths << QStringLiteral("Processing.NDI.Lib.x64.dll");
#elif defined(Q_OS_MACOS)
    paths << QStringLiteral("/usr/local/lib/libndi.dylib") << QStringLiteral("/Library/NDI SDK for Apple/lib/macOS/libndi.dylib")
          << QStringLiteral("libndi.dylib");
#else
    paths << QStringLiteral("libndi.so.6") << QStringLiteral("libndi.so.5") << QStringLiteral("libndi.so")
          << QStringLiteral("/usr/lib/libndi.so") << QStringLiteral("/usr/local/lib/libndi.so");
#endif
    return paths;
}

} // namespace

struct NdiSink::Api
{
    bool (*initialize)() = nullptr;
    void (*destroy)() = nullptr;
    void *(*sendCreate)(const NdiSendCreate *) = nullptr;
    void (*sendDestroy)(void *) = nullptr;
    void (*sendVideo)(void *, const NdiVideoFrameV2 *) = nullptr;
};

NdiSink::NdiSink(const QString &sourceName, int fps, QObject *parent)
    : QObject(parent), m_api(std::make_unique<Api>()), m_name(sourceName.toUtf8()), m_fps(std::max(1, fps))
{
    for (const QString &path : candidates()) {
        m_library.setFileName(path);
        if (m_library.load())
            break;
    }
    if (!m_library.isLoaded()) {
        m_status = tr("The NDI Runtime is not installed (get it from ndi.video), so there is no NDI output");
        return;
    }
    m_api->initialize = reinterpret_cast<bool (*)()>(m_library.resolve("NDIlib_initialize"));
    m_api->destroy = reinterpret_cast<void (*)()>(m_library.resolve("NDIlib_destroy"));
    m_api->sendCreate = reinterpret_cast<void *(*)(const NdiSendCreate *)>(m_library.resolve("NDIlib_send_create"));
    m_api->sendDestroy = reinterpret_cast<void (*)(void *)>(m_library.resolve("NDIlib_send_destroy"));
    m_api->sendVideo = reinterpret_cast<void (*)(void *, const NdiVideoFrameV2 *)>(m_library.resolve("NDIlib_send_send_video_v2"));
    if (!m_api->initialize || !m_api->destroy || !m_api->sendCreate || !m_api->sendDestroy || !m_api->sendVideo) {
        m_status = tr("%1 is not an NDI Runtime Lumen can use").arg(QFileInfo(m_library.fileName()).fileName());
        m_library.unload();
        return;
    }
    if (!m_api->initialize()) {
        m_status = tr("The NDI Runtime would not start (this processor may not be supported)");
        m_library.unload();
        return;
    }
    const NdiSendCreate create{m_name.constData(), nullptr, false, false};
    m_sender = m_api->sendCreate(&create);
    if (!m_sender) {
        m_api->destroy();
        m_status = tr("The NDI source could not be created");
        return;
    }
    m_status = tr("Sending as \"%1\"").arg(sourceName);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1000 / m_fps);
    connect(&m_timer, &QTimer::timeout, this, &NdiSink::send);
    m_timer.start();
}

NdiSink::~NdiSink()
{
    if (m_sender) {
        m_timer.stop();
        m_api->sendDestroy(m_sender);
        m_api->destroy();
    }
}

void NdiSink::frame(const QImage &premultiplied)
{
    if (!m_sender)
        return;
    // NDI's BGRA is straight alpha; on little-endian machines ARGB32 is laid
    // out as B, G, R, A in memory.
    m_frame = premultiplied.convertToFormat(QImage::Format_ARGB32);
}

void NdiSink::send()
{
    if (!m_sender || m_frame.isNull())
        return;
    NdiVideoFrameV2 video{};
    video.xres = m_frame.width();
    video.yres = m_frame.height();
    video.FourCC = fourCC('B', 'G', 'R', 'A');
    video.frame_rate_N = m_fps * 1000;
    video.frame_rate_D = 1000;
    video.picture_aspect_ratio = float(m_frame.width()) / float(std::max(1, m_frame.height()));
    video.frame_format_type = 1;
    video.timecode = std::numeric_limits<std::int64_t>::max();
    video.p_data = m_frame.bits();
    video.line_stride_in_bytes = int(m_frame.bytesPerLine());
    m_api->sendVideo(m_sender, &video);
}

} // namespace lumen::render
