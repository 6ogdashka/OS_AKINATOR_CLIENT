#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include "server.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("akinator-server"));
    const QString raw = qEnvironmentVariable("GAME_SERVER_PORT", QStringLiteral("9999"));
    bool ok = false;
    int port = raw.toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        qWarning().noquote()
            << QStringLiteral("Некорректный GAME_SERVER_PORT='%1', используется порт 9999").arg(raw);
        port = 9999;
    }
    Server server;
    if (!server.start(static_cast<quint16>(port))) {
        qCritical().noquote() << QStringLiteral("Не удалось запустить сервер на порту %1").arg(port);
        return 1;
    }
    return app.exec();
}
