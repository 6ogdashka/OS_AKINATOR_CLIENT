#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpSocket>

class Connection : public QObject
{
    Q_OBJECT

public:
    explicit Connection(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const;
    void send(const QString& command, const QStringList& args = {});

signals:
    void connected();
    void messageReceived(const QStringList& parts);
    void errorOccurred(const QString& text);
    void connectionClosed();

private:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError();

    QTcpSocket socket_;
    QByteArray buffer_;
};
