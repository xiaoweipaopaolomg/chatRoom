#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
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
    transport.messageReceived(QJsonObject{{"type", "response.ok"}, {"request_id", "1"}});
    QVERIFY(!send->isEnabled());

    model.createRoom(QStringLiteral("x"));

    const QJsonObject room{{"id", 1},
                           {"name", "x"},
                           {"members", QJsonArray{QJsonValue{"alice"}}}};
    transport.messageReceived(
        QJsonObject{{"type", "room.snapshot"}, {"request_id", "2"}, {"room", room}});
    QVERIFY(send->isEnabled());

    auto* title = window.findChild<QLabel*>("roomTitle");
    auto* members = window.findChild<QListWidget*>("memberList");
    auto* timeline = window.findChild<QListWidget*>("timeline");
    QVERIFY(title);
    QVERIFY(members);
    QVERIFY(timeline);
    QCOMPARE(title->text(), QStringLiteral("x"));
    QCOMPARE(members->count(), 1);

    transport.messageReceived(QJsonObject{{"type", "chat.message"},
                                           {"room_id", 1},
                                           {"message_id", 1},
                                           {"sender", "alice"},
                                           {"text", "hello"}});
    QCOMPARE(timeline->count(), 1);
    QVERIFY(timeline->itemWidget(timeline->item(0)));
  }
};

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
