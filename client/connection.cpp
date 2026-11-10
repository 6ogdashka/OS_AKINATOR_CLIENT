#include "connection.h"

#include "protocol.h"

#include <QDebug>

Connection::Connection(QObject* parent)
    : QObject(parent)
{
    connect(&socket_, &QTcpSocket::connected, this, &Connection::onConnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &Connection::onReadyRead);
    connect(&socket_, &QTcpSocket::disconnected, this, &Connection::onDisconnected);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &Connection::onSocketError);
}

void Connection::connectToServer(const QString& host, quint16 port)
{
    buffer_.clear();
    qInfo().noquote() << QStringLiteral("Подключение к %1:%2").arg(host).arg(port);
    socket_.abort();
    socket_.connectToHost(host, port);
}

void Connection::disconnectFromServer()
{
    if (socket_.state() == QAbstractSocket::UnconnectedState) {
        return;
    }
    socket_.disconnectFromHost();
}

bool Connection::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void Connection::send(const QString& command, const QStringList& args)
{
    if (!isConnected()) {
        qWarning().noquote() << QStringLiteral("Сообщение не отправлено: нет соединения");
        return;
    }

    QStringList parts{command};
    parts += args;
    socket_.write(proto::makeLine(parts));
    socket_.flush();
}

void Connection::onConnected()
{
    qInfo().noquote() << QStringLiteral("Соединение установлено");
    emit connected();
}

void Connection::onDisconnected()
{
    qInfo().noquote() << QStringLiteral("Соединение закрыто");
    buffer_.clear();
    emit connectionClosed();
}

void Connection::onSocketError()
{
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        qWarning().noquote() << QStringLiteral("Ошибка подключения: %1").arg(socket_.errorString());
        emit errorOccurred(socket_.errorString());
        emit connectionClosed();
    }
}

void Connection::onReadyRead()
{
    buffer_.append(socket_.readAll());

    if (buffer_.size() > proto::kMaxLineBytes) {
        qWarning().noquote() << QStringLiteral("Слишком длинное сообщение от сервера");
        buffer_.clear();
        emit errorOccurred(QStringLiteral("Сервер прислал слишком длинное сообщение"));
        socket_.abort();
        return;
    }

    const QStringList lines = proto::takeLines(&buffer_);
    for (const QString& line : lines) {
        const QStringList parts = proto::parseLine(line);
        if (!parts.isEmpty()) {
            emit messageReceived(parts);
        }
    }
}
