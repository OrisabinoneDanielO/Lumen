#include "lumen/core/render/protocol.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace lumen::core::render {

QByteArray encode(QJsonObject message)
{
    message.insert(QStringLiteral("v"), protocolVersion);
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

std::optional<QJsonObject> decode(const QByteArray &line)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    return document.object();
}

QJsonObject okReply(QJsonObject fields)
{
    fields.insert(QStringLiteral("ok"), true);
    return fields;
}

QJsonObject errorReply(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QString defaultServerName()
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("USERNAME");
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9_-]"));
    user.replace(unsafe, QStringLiteral("_"));
    return QStringLiteral("lumen-render-") + (user.isEmpty() ? QStringLiteral("default") : user);
}

} // namespace lumen::core::render
