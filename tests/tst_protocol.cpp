#include <QtTest>

#include "protocol.h"

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void parseLine();
    void readInt();
    void makeLine();
    void takeLines();
    void partialLine();
};

void TestProtocol::parseLine()
{
    const QStringList ask = proto::parseLine(QStringLiteral("ASK 12"));
    QCOMPARE(ask, QStringList({QStringLiteral("ASK"), QStringLiteral("12")}));

    const QStringList table = proto::parseLine(QStringLiteral("  TABLE  "));
    QCOMPARE(table, QStringList({QStringLiteral("TABLE")}));

    QVERIFY(proto::parseLine(QString()).isEmpty());
}

void TestProtocol::readInt()
{
    const QStringList parts = proto::parseLine(QStringLiteral("REMOVE 3 17 42"));
    QCOMPARE(parts.size(), 4);

    int value = -1;
    QVERIFY(proto::readInt(parts, 1, &value));
    QCOMPARE(value, 3);

    QVERIFY(!proto::readInt(parts, 0, &value));
    QVERIFY(!proto::readInt(parts, 4, &value));
}

void TestProtocol::makeLine()
{
    QCOMPARE(proto::makeLine({QStringLiteral("START")}),
             QByteArrayLiteral("START\n"));
    QCOMPARE(proto::makeLine({QStringLiteral("ANSWER"), QStringLiteral("12"),
                              QStringLiteral("yes"), QStringLiteral("31")}),
             QByteArrayLiteral("ANSWER 12 yes 31\n"));
    QCOMPARE(proto::makeLine({QStringLiteral("ERROR"),
                              QStringLiteral("Сейчас не ваш ход")}),
             QByteArrayLiteral("ERROR Сейчас не ваш ход\n"));
}

void TestProtocol::takeLines()
{
    QByteArray buffer = QByteArrayLiteral(
        "ASK 1\n"
        "ANSWER 1 yes 30\r\n"
        "\n"
        "HINTS 1 1 1\n");

    const QStringList lines = proto::takeLines(&buffer);
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0), QStringLiteral("ASK 1"));
    QCOMPARE(lines.at(1), QStringLiteral("ANSWER 1 yes 30"));
    QCOMPARE(lines.at(2), QStringLiteral("HINTS 1 1 1"));
    QVERIFY(buffer.isEmpty());
}

void TestProtocol::partialLine()
{
    QByteArray buffer = QByteArrayLiteral("ASK 5");
    QVERIFY(proto::takeLines(&buffer).isEmpty());
    QCOMPARE(buffer, QByteArrayLiteral("ASK 5"));

    buffer.append('\n');
    const QStringList completed = proto::takeLines(&buffer);
    QCOMPARE(completed.size(), 1);
    QCOMPARE(completed.at(0), QStringLiteral("ASK 5"));
    QVERIFY(buffer.isEmpty());
}

QTEST_APPLESS_MAIN(TestProtocol)

#include "tst_protocol.moc"
