#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include "qt_client/chat_transport.h"
#include "qt_client/client_model.h"

using namespace chatroom::qt_client;

class ClientModelTest : public QObject {
  Q_OBJECT

 private slots:
  void resetsOnDisconnect() {
    ChatTransport transport;
    ClientModel model(&transport);

    transport.connected();
    QCOMPARE(model.state(), ClientModel::State::Connected);

    transport.messageReceived(QJsonObject{{"type", "response.ok"}});
    QCOMPARE(model.state(), ClientModel::State::Identified);

    const QJsonObject room{{"id", 7},
                           {"name", "x"},
                           {"members", QJsonArray{QJsonValue{"alice"}}}};
    transport.messageReceived(
        QJsonObject{{"type", "room.snapshot"}, {"room", room}});
    QCOMPARE(model.state(), ClientModel::State::InRoom);
    QCOMPARE(model.members(), QStringList{"alice"});

    transport.disconnected();
    QCOMPARE(model.state(), ClientModel::State::Disconnected);
    QVERIFY(model.members().isEmpty());
  }
};

QTEST_MAIN(ClientModelTest)
#include "client_model_test.moc"
