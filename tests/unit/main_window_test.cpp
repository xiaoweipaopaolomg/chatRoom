#include <QtTest>
#include <QPushButton>
#include "qt_client/chat_transport.h"
#include "qt_client/client_model.h"
#include "qt_client/main_window.h"
using namespace chatroom::qt_client;
class MainWindowTest:public QObject{Q_OBJECT private slots:void sendEnabledOnlyInsideRoom(){ChatTransport transport;ClientModel model(&transport);MainWindow window(&model);auto* send=window.findChild<QPushButton*>("sendButton");QVERIFY(send);QVERIFY(!send->isEnabled());transport.connected();transport.messageReceived({{"type","response.ok"}});QVERIFY(!send->isEnabled());transport.messageReceived({{"type","room.snapshot"},{"room",{{"id",1},{"name","x"},{"members",QJsonArray{"alice"}}}}});QVERIFY(send->isEnabled());}};
QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
