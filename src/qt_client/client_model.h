#pragma once

#include <QObject>
#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QStringList>

#include "qt_client/chat_transport.h"

namespace chatroom::qt_client {

class ClientModel : public QObject {
  Q_OBJECT
 public:
  enum class State { Disconnected, Connected, Identified, InRoom };
  Q_ENUM(State)
  explicit ClientModel(ChatTransport* transport, QObject* parent = nullptr);
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] quint64 roomId() const noexcept { return room_id_; }
  [[nodiscard]] const QString& nickname() const noexcept { return nickname_; }
  [[nodiscard]] const QString& roomName() const noexcept { return room_name_; }
  [[nodiscard]] QStringList members() const;

  void connectToServer(const QString& host, quint16 port, const QString& nickname);
  void disconnectFromServer();
  void createRoom(const QString& name);
  void joinRoom(quint64 room_id);
  void leaveRoom();
  void sendChat(const QString& text);

 signals:
  void stateChanged(chatroom::qt_client::ClientModel::State state);
  void membersChanged(const QStringList& members);
  void roomChanged(quint64 room_id, const QString& room_name);
  void chatReceived(const QString& sender, const QString& text);
  void statusMessage(const QString& message);
  void diagnosticMessage(const QString& message);

 private slots:
  void onConnected();
  void onDisconnected();
  void onMessage(const QJsonObject& message);

 private:
  QString nextRequestId();
  void setState(State state);
  void send(const QString& type, QJsonObject payload);
  ChatTransport* transport_;
  State state_{State::Disconnected};
  QString nickname_;
  QString room_name_;
  quint64 request_id_{0};
  quint64 room_id_{0};
  QSet<QString> members_;
  QHash<QString, QString> pending_;
};

}  // namespace chatroom::qt_client
