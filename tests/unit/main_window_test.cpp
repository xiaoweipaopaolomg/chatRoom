#include <QJsonArray>
#include <QJsonObject>
#include <QPushButton>
#include <QtTest>

#include "qt_client/chat_transport.h"
#include "qt_client/client_model.h"
#include "qt_client/main_window.h"

using namespace chatroom::qt_client;

class MainWindowTest : public QObject {
  Q_OBJECT

 private slots:
  void sendEnabledOnlyInsideRoom() {
    ChatTransport transport;
    ClientModel model(&transport);
    MainWindow window(&model);

    auto* send = window.findChild<QPushButton*>("sendButton");
    QVERIFY(send);
    QVERIFY(!send->isEnabled());

    transport.connected();
    transport.messageReceived(QJsonObject{{"type", "response.ok"}});
    QVERIFY(!send->isEnabled());

    const QJsonObject room{{"id", 1},
                           {"name", "x"},
                           {"members", QJsonArray{QJsonValue{"alice"}}}};
    transport.messageReceived(
        QJsonObject{{"type", "room.snapshot"}, {"room", room}});
    QVERIFY(send->isEnabled());
  }
};

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
