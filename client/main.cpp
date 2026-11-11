#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QIcon>
#include <QString>

#include "connection.h"
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("akinator-client"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QFile style(QStringLiteral(":/style.qss"));
    if (style.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(style.readAll()));
    }
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/state_idle.png")));
    QString host = qEnvironmentVariable("GAME_SERVER_HOST").trimmed();
    if (host.isEmpty()) {
        host = QStringLiteral("127.0.0.1");
    }
    QString warning;
    const QString rawPort = qEnvironmentVariable("GAME_SERVER_PORT", QStringLiteral("9999"));
    bool ok = false;
    int port = rawPort.toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        warning = QStringLiteral("Некорректный GAME_SERVER_PORT='%1', используется порт 9999").arg(rawPort);
        qWarning().noquote() << warning;
        port = 9999;
    }
    Connection connection;
    MainWindow window(&connection, host, port, warning);
    window.show();
    return app.exec();
}
