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
    for (int index; (index = buffer->indexOf('\n', start)) >= 0; start = index + 1) {
        const QString line = QString::fromUtf8(buffer->mid(start, index - start)).trimmed();
        if (!line.isEmpty()) {
            lines << line;
        }
    }
    buffer->remove(0, start);
    return lines;
}

inline QStringList parseLine(const QString& line)
{
    return line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

inline bool readInt(const QStringList& parts, int index, int* out)
{
    bool ok = false;
    const int value = (index >= 0 && index < parts.size()) ? parts.at(index).toInt(&ok) : 0;
    if (ok && out) {
        *out = value;
    }
    return ok;
}

inline QByteArray makeLine(const QStringList& parts)
{
    return (parts.join(QLatin1Char(' ')) + QLatin1Char('\n')).toUtf8();
}
}
