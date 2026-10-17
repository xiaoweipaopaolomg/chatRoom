#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include <QTcpSocket>

namespace chatroom::qt_client {

class ChatTransport : public QObject {
  Q_OBJECT
 public:
  explicit ChatTransport(QObject* parent = nullptr);
  void connectToHost(const QString& host, quint16 port);
  void disconnectFromHost();
  void sendCommand(const QJsonObject& command);

 signals:
  void connected();
  void disconnected();
  void messageReceived(const QJsonObject& message);
  void protocolError(const QString& message);

 private slots:
  void onReadyRead();

 private:
  QTcpSocket socket_;
  QByteArray input_;
};

}  // namespace chatroom::qt_client
