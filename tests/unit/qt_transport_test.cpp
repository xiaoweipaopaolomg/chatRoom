#include <QtTest>
#include <QTcpServer>
#include <QtEndian>
#include <QJsonDocument>
#include "qt_client/chat_transport.h"

using namespace chatroom::qt_client;
namespace {
QByteArray Frame(QJsonObject value){auto body=QJsonDocument(value).toJson(QJsonDocument::Compact);QByteArray result(4,Qt::Uninitialized);qToBigEndian<quint32>(static_cast<quint32>(body.size()),result.data());return result+body;}
}
class QtTransportTest:public QObject{Q_OBJECT private slots:void decodesFragmentedAndCoalescedFrames(){QTcpServer fake;QVERIFY(fake.listen(QHostAddress::LocalHost));ChatTransport transport;QSignalSpy messages(&transport,&ChatTransport::messageReceived);transport.connectToHost("127.0.0.1",fake.serverPort());QVERIFY(fake.waitForNewConnection());auto* peer=fake.nextPendingConnection();auto bytes=Frame({{"type","pong"}})+Frame({{"type","room.snapshot"}});peer->write(bytes.left(3));peer->write(bytes.mid(3));QTRY_COMPARE(messages.count(),2);}};
QTEST_MAIN(QtTransportTest)
#include "qt_transport_test.moc"
