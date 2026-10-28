#include "qt_client/chat_transport.h"

#include <QtEndian>
#include <QJsonDocument>

#include "protocol/limits.h"

namespace chatroom::qt_client {

ChatTransport::ChatTransport(QObject* parent) : QObject(parent) {
  connect(&socket_, &QTcpSocket::connected, this, [this] {
    disconnect_requested_ = false;
    emit connected();
  });
  connect(&socket_, &QTcpSocket::disconnected, this, [this] {
    emit disconnected();
    disconnect_requested_ = false;
  });
  connect(&socket_, &QTcpSocket::readyRead, this, &ChatTransport::onReadyRead);
  connect(&socket_, &QTcpSocket::errorOccurred, this,
          [this](QAbstractSocket::SocketError error) {
            if (disconnect_requested_ &&
                error == QAbstractSocket::RemoteHostClosedError) {
              return;
            }
            emit transportError(socket_.errorString());
          });
}

void ChatTransport::connectToHost(const QString& host, quint16 port) {
  disconnect_requested_ = false;
  input_.clear();
  socket_.connectToHost(host, port);
}
void ChatTransport::disconnectFromHost() {
  disconnect_requested_ = true;
  socket_.disconnectFromHost();
}

void ChatTransport::sendCommand(const QJsonObject& command) {
  const QByteArray body = QJsonDocument(command).toJson(QJsonDocument::Compact);
  if (body.isEmpty() ||
      body.size() > static_cast<qsizetype>(protocol::kMaximumFrameBodyBytes)) {
    emit protocolError(QStringLiteral("outbound JSON exceeds frame limit")); return;
  }
  QByteArray frame(4, Qt::Uninitialized);
  qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
  frame.append(body); socket_.write(frame);
}

void ChatTransport::onReadyRead() {
  input_.append(socket_.readAll());
  for (;;) {
    if (input_.size() < 4) return;
    const auto length = qFromBigEndian<quint32>(input_.constData());
    if (length == 0 || length > protocol::kMaximumFrameBodyBytes) {
      emit protocolError(QStringLiteral("invalid inbound frame length"));
      socket_.abort(); input_.clear(); return;
    }
    if (input_.size() < 4 + static_cast<int>(length)) return;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input_.mid(4, static_cast<int>(length)), &error);
    input_.remove(0, 4 + static_cast<int>(length));
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
      emit protocolError(QStringLiteral("invalid JSON message")); socket_.abort(); return;
    }
    emit messageReceived(document.object());
  }
}

}  // namespace chatroom::qt_client
