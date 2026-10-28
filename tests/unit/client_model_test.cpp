#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QtTest>

#include "qt_client/chat_transport.h"
#include "qt_client/client_model.h"

using namespace chatroom::qt_client;

class ClientModelTest : public QObject {
  Q_OBJECT

 private slots:
  void connectsToLiveServerWhenConfigured() {
    const auto host = qEnvironmentVariable("CHATROOM_LIVE_HOST");
    if (host.isEmpty()) QSKIP("CHATROOM_LIVE_HOST is not set");
    bool valid_port = false;
    const auto port = qEnvironmentVariableIntValue("CHATROOM_LIVE_PORT", &valid_port);

    ChatTransport transport;
    ClientModel model(&transport);
    QSignalSpy errors(&model, &ClientModel::statusMessage);
    model.connectToServer(host, valid_port ? static_cast<quint16>(port) : 7777,
                          QStringLiteral("qt-probe-%1")
                              .arg(QCoreApplication::applicationPid()));

    QTRY_COMPARE_WITH_TIMEOUT(model.state(), ClientModel::State::Identified, 5000);
    QCOMPARE(errors.count(), 0);
    model.disconnectFromServer();
    QTRY_COMPARE_WITH_TIMEOUT(model.state(), ClientModel::State::Disconnected, 5000);
  }

  void resetsOnDisconnect() {
    ChatTransport transport;
    ClientModel model(&transport);

    transport.connected();
    QCOMPARE(model.state(), ClientModel::State::Connected);

    transport.messageReceived(QJsonObject{{"type", "response.ok"}, {"request_id", "1"}});
    QCOMPARE(model.state(), ClientModel::State::Identified);

    model.createRoom(QStringLiteral("x"));

    const QJsonObject room{{"id", 7},
                           {"name", "x"},
                           {"members", QJsonArray{QJsonValue{"alice"}}}};
    transport.messageReceived(
        QJsonObject{{"type", "room.snapshot"}, {"request_id", "2"}, {"room", room}});
    QCOMPARE(model.state(), ClientModel::State::InRoom);
    QCOMPARE(model.members(), QStringList{"alice"});
    QCOMPARE(model.roomName(), QStringLiteral("x"));

    transport.messageReceived(QJsonObject{{"type", "response.ok"}, {"request_id", "unknown"}});
    QCOMPARE(model.state(), ClientModel::State::InRoom);

    model.leaveRoom();
    transport.messageReceived(QJsonObject{{"type", "response.ok"}, {"request_id", "3"}});
    QCOMPARE(model.state(), ClientModel::State::Identified);
    QVERIFY(model.roomName().isEmpty());

    transport.disconnected();
    QCOMPARE(model.state(), ClientModel::State::Disconnected);
    QVERIFY(model.members().isEmpty());
    QVERIFY(model.roomName().isEmpty());
  }
};

QTEST_MAIN(ClientModelTest)
#include "client_model_test.moc"
