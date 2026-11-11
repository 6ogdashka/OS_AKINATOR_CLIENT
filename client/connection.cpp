#include "connection.h"

#include "protocol.h"

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
    socket_.abort();
    socket_.connectToHost(host, port);
}

void Connection::disconnectFromServer()
{
    if (socket_.state() != QAbstractSocket::UnconnectedState) socket_.disconnectFromHost();
}

bool Connection::isConnected() const { return socket_.state() == QAbstractSocket::ConnectedState; }

void Connection::send(const QString& command, const QStringList& args)
{
    if (!isConnected()) return;
    QStringList parts{command};
    parts += args;
    socket_.write(proto::makeLine(parts));
    socket_.flush();
}

void Connection::onConnected() { emit connected(); }

void Connection::onReadyRead()
{
    buffer_.append(socket_.readAll());
    if (buffer_.size() > proto::kMaxLineBytes) {
        buffer_.clear();
        emit errorOccurred(QStringLiteral("Сервер прислал слишком длинное сообщение"));
        socket_.abort();
        return;
    }
    for (const QString& line : proto::takeLines(&buffer_)) {
        emit messageReceived(proto::parseLine(line));
    }
}

void Connection::onDisconnected()
{
    buffer_.clear();
    emit connectionClosed();
}

void Connection::onSocketError()
{
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        emit errorOccurred(socket_.errorString());
        emit connectionClosed();
    }
}
