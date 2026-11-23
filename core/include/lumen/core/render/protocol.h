#pragma once

// Messages between the operator UI and the render process (lumen-render).
//
// Each message is one line of compact JSON over a local socket that only the
// same user can open. Every request gets exactly one reply line, in order. A
// request may carry an "id", which its reply echoes.
//
// (Examples show v2; the current version is protocolVersion.)
//   {"v":2,"cmd":"scene","scene":{...}}  -> {"v":2,"ok":true}
//       The whole on-air scene (lumen/core/show/scene.h). Snapshots with a
//       sequence lower than the one on screen are refused as stale.
//   {"v":2,"cmd":"getScene"}  -> {"v":2,"ok":true,"scene":{...}}
//   {"v":2,"cmd":"status"}    -> {"v":2,"ok":true,"frames":123,"pid":4567,
//                                 "presentedSequence":12}  (scene in the last frame)
//   {"v":2,"cmd":"quit"}      -> {"v":2,"ok":true}, then the renderer exits
//   {"v":4,"cmd":"preload","video":"/path"}  -> opens a video ahead of use
//       (the item in Preview), so it starts at once when sent live
//   "status" also reports "video": {frames, dropped, loops, stalls,
//       maxGapMs, gpuFrames, startMs, starts, playing} for the videos on screen
//   and "timing": {tickMaxGapMs, tickStalls, swapMaxGapMs, swapStalls}: gaps
//       over 100 ms in the render process's own main loop and frame swaps
//   Only with --test-mode:
//   {"v":2,"cmd":"grab","path":"/tmp/f.png"}
//   {"v":2,"cmd":"clock","mode":"manual"|"real"}
//   {"v":2,"cmd":"clock","advanceMs":250}   (manual clock only)
//   {"v":5,"cmd":"clock","wallMs":1795440000000}  sets the wall clock timers use (test mode)
//
// Failures reply {"v":2,"ok":false,"error":"..."}. If the UI disconnects
// without "quit" (for example it crashed), the renderer keeps showing the
// last frame and finishes any fade in progress.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace lumen::core::render {

// v3 (batch 8): text content may carry a theme style and a footer.
// v4 (batch 9): video content; "preload"; video statistics in "status".
// v5 (batch 12): timer content; "clock" takes "wallMs".
// v6 (batch 13): stage displays (lumen-render --stage): "stage" with the
//     stage state; "texts" (test mode); "status" reports "kind" and "screen".
// v7 (batch 15): stream outputs (lumen-render --stream): "status" kind
//     "stream" and "stream" (where its frames go).
inline constexpr int protocolVersion = 7;

// Longest line either side accepts, to bound memory use.
inline constexpr qsizetype maxLineBytes = 1024 * 1024;

// Adds the protocol version and encodes the message as one line.
QByteArray encode(QJsonObject message);

// Parses one line (with or without the trailing newline). Returns nothing if
// it is not a JSON object.
std::optional<QJsonObject> decode(const QByteArray &line);

// Replies.
QJsonObject okReply(QJsonObject fields = {});
QJsonObject errorReply(const QString &message);

// Local socket name for this user's renderer.
QString defaultServerName();

} // namespace lumen::core::render
