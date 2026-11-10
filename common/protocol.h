#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace proto {
constexpr int kMaxLineBytes = 64 * 1024;

inline QStringList takeLines(QByteArray* buffer)
{
    QStringList lines;
    if (!buffer) {
        return lines;
    }

    int start = 0;
    int index = 0;
    while ((index = buffer->indexOf('\n', start)) >= 0) {
        QString line = QString::fromUtf8(buffer->mid(start, index - start)).trimmed();
        start = index + 1;
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    if (start > 0) {
        buffer->remove(0, start);
    }
    return lines;
}

inline QStringList parseLine(const QString& line)
{
    return line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

inline bool readInt(const QStringList& parts, int index, int* out)
{
    if (index < 0 || index >= parts.size()) {
        return false;
    }
    bool ok = false;
    const int value = parts.at(index).toInt(&ok);
    if (!ok) {
        return false;
    }
    if (out) {
        *out = value;
    }
    return true;
}

inline QByteArray makeLine(const QStringList& parts)
{
    return (parts.join(QLatin1Char(' ')) + QLatin1Char('\n')).toUtf8();
}
}
