#include "qt_client/client_model.h"

#include <QJsonArray>

namespace chatroom::qt_client {

ClientModel::ClientModel(ChatTransport* transport, QObject* parent)
    : QObject(parent), transport_(transport) {
  connect(transport_, &ChatTransport::connected, this, &ClientModel::onConnected);
  connect(transport_, &ChatTransport::disconnected, this, &ClientModel::onDisconnected);
  connect(transport_, &ChatTransport::messageReceived, this, &ClientModel::onMessage);
  connect(transport_, &ChatTransport::protocolError, this, &ClientModel::diagnosticMessage);
}

QStringList ClientModel::members() const { auto list = members_.values(); list.sort(); return list; }
QString ClientModel::nextRequestId() { return QString::number(++request_id_); }
void ClientModel::setState(State state) { if (state_ != state) { state_ = state; emit stateChanged(state); } }
void ClientModel::send(const QString& type, QJsonObject payload) {
  transport_->sendCommand({{"type", type}, {"request_id", nextRequestId()}, {"payload", payload}});
}

void ClientModel::connectToServer(const QString& host, quint16 port, const QString& nickname) {
  if (state_ != State::Disconnected) return;
  nickname_ = nickname; transport_->connectToHost(host, port);
}
void ClientModel::disconnectFromServer() { transport_->disconnectFromHost(); }
void ClientModel::createRoom(const QString& name) {
  if (state_ == State::Identified) send("room.create", {{"name", name}});
}
void ClientModel::joinRoom(quint64 room_id) {
  if (state_ == State::Identified) send("room.join", {{"room_id", static_cast<qint64>(room_id)}});
}
void ClientModel::leaveRoom() { if (state_ == State::InRoom) send("room.leave", {}); }
void ClientModel::sendChat(const QString& text) {
  if (state_ == State::InRoom) send("chat.send", {{"text", text}});
}

void ClientModel::onConnected() {
  setState(State::Connected); send("session.hello", {{"nickname", nickname_}});
}
void ClientModel::onDisconnected() {
  room_id_ = 0; members_.clear(); emit membersChanged({}); setState(State::Disconnected);
}

void ClientModel::onMessage(const QJsonObject& message) {
  const QString type = message.value("type").toString();
  if (type == "response.ok") {
    if (state_ == State::Connected) setState(State::Identified);
    else if (state_ == State::InRoom) { room_id_ = 0; members_.clear(); emit membersChanged({}); setState(State::Identified); }
  } else if (type == "response.error") {
    const auto error = message.value("error").toObject();
    emit statusMessage(error.value("code").toString() + ": " + error.value("message").toString());
  } else if (type == "room.snapshot") {
    const auto room = message.value("room").toObject();
    room_id_ = static_cast<quint64>(room.value("id").toInteger()); members_.clear();
    for (const auto& member : room.value("members").toArray()) members_.insert(member.toString());
    emit membersChanged(members()); setState(State::InRoom);
  } else if (type == "member.joined" || type == "member.left") {
    if (static_cast<quint64>(message.value("room_id").toInteger()) != room_id_) {
      emit diagnosticMessage(QStringLiteral("ignored event for another room")); return;
    }
    const auto nickname = message.value("nickname").toString();
    if (type == "member.joined") members_.insert(nickname); else members_.remove(nickname);
    emit membersChanged(members());
  } else if (type == "chat.message") {
    if (static_cast<quint64>(message.value("room_id").toInteger()) == room_id_)
      emit chatReceived(message.value("sender").toString(), message.value("text").toString());
  }
}

}  // namespace chatroom::qt_client
