#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QtEndian>
#include <QtTest>

#include "qt_client/chat_transport.h"

using namespace chatroom::qt_client;

namespace {
QByteArray Frame(const QJsonObject& value) {
  const auto body = QJsonDocument(value).toJson(QJsonDocument::Compact);
  QByteArray result(4, Qt::Uninitialized);
  qToBigEndian<quint32>(static_cast<quint32>(body.size()), result.data());
  return result + body;
}
}  // namespace

class QtTransportTest : public QObject {
  Q_OBJECT

 private slots:
  void decodesFragmentedAndCoalescedFrames() {
    QTcpServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));

    ChatTransport transport;
    QSignalSpy messages(&transport, &ChatTransport::messageReceived);
    transport.connectToHost(QStringLiteral("127.0.0.1"), fake.serverPort());
    QVERIFY(fake.waitForNewConnection());

    auto* peer = fake.nextPendingConnection();
    const auto bytes = Frame(QJsonObject{{"type", "pong"}}) +
                       Frame(QJsonObject{{"type", "room.snapshot"}});
    peer->write(bytes.left(3));
    peer->write(bytes.mid(3));

    QTRY_COMPARE(messages.count(), 2);
  }
};

QTEST_MAIN(QtTransportTest)
#include "qt_transport_test.moc"
