#pragma once

// Messages between the operator UI and the render process (lumen-render).
//
// Each message is one line of compact JSON over a local socket that only the
// same user can open. Every request gets exactly one reply line.
//
//   {"v":1,"cmd":"show","colour":"#14213D","text":"..."}  -> {"v":1,"ok":true}
//   {"v":1,"cmd":"status"}  -> {"v":1,"ok":true,"frames":123,"pid":4567}
//   {"v":1,"cmd":"grab","path":"/tmp/f.png"}  (only with --test-mode)
//   {"v":1,"cmd":"quit"}    -> {"v":1,"ok":true}, then the renderer exits
//
// Failures reply {"v":1,"ok":false,"error":"..."}. If the UI disconnects
// without "quit" (for example it crashed), the renderer keeps showing the
// last frame. Batch 4 replaces this with the show controller protocol.

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace lumen::core::render {

inline constexpr int protocolVersion = 1;

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
