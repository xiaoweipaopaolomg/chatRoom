#include "qt_client/main_window.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "qt_client/client_model.h"

namespace chatroom::qt_client {

MainWindow::MainWindow(ClientModel* model, QWidget* parent) : QMainWindow(parent), model_(model) {
  auto* central = new QWidget(this); auto* root = new QVBoxLayout(central);
  auto* connection = new QHBoxLayout;
  host_ = new QLineEdit("127.0.0.1"); host_->setObjectName("hostEdit");
  port_ = new QLineEdit("7777"); port_->setObjectName("portEdit");
  nickname_ = new QLineEdit; nickname_->setObjectName("nicknameEdit");
  connect_ = new QPushButton(tr("Connect")); connect_->setObjectName("connectButton");
  connection->addWidget(host_); connection->addWidget(port_); connection->addWidget(nickname_); connection->addWidget(connect_);
  root->addLayout(connection);
  auto* rooms = new QHBoxLayout;
  room_name_ = new QLineEdit; room_name_->setPlaceholderText(tr("Room name")); room_name_->setObjectName("roomNameEdit");
  create_ = new QPushButton(tr("Create")); create_->setObjectName("createButton");
  room_id_ = new QLineEdit; room_id_->setPlaceholderText(tr("Room ID")); room_id_->setObjectName("roomIdEdit");
  join_ = new QPushButton(tr("Join")); join_->setObjectName("joinButton");
  leave_ = new QPushButton(tr("Leave")); leave_->setObjectName("leaveButton");
  rooms->addWidget(room_name_); rooms->addWidget(create_); rooms->addWidget(room_id_); rooms->addWidget(join_); rooms->addWidget(leave_); root->addLayout(rooms);
  auto* content = new QHBoxLayout; timeline_ = new QTextEdit; timeline_->setReadOnly(true); timeline_->setObjectName("timeline");
  members_ = new QListWidget; members_->setObjectName("memberList"); content->addWidget(timeline_, 4); content->addWidget(members_, 1); root->addLayout(content);
  auto* input = new QHBoxLayout; composer_ = new QPlainTextEdit; composer_->setMaximumHeight(90); composer_->setObjectName("messageEdit");
  send_ = new QPushButton(tr("Send")); send_->setObjectName("sendButton"); input->addWidget(composer_); input->addWidget(send_); root->addLayout(input); setCentralWidget(central);

  connect(connect_, &QPushButton::clicked, this, [this] {
    if (model_->state() == ClientModel::State::Disconnected) {
      bool ok = false; const auto value = port_->text().toUShort(&ok);
      if (!ok || nickname_->text().toUtf8().isEmpty() || nickname_->text().toUtf8().size() > 32) { statusBar()->showMessage(tr("Invalid port or nickname")); return; }
      model_->connectToServer(host_->text(), value, nickname_->text());
    } else model_->disconnectFromServer();
  });
  connect(create_, &QPushButton::clicked, this, [this] { if (room_name_->text().toUtf8().size() <= 64) model_->createRoom(room_name_->text()); });
  connect(join_, &QPushButton::clicked, this, [this] { bool ok=false; auto id=room_id_->text().toULongLong(&ok); if(ok && id) model_->joinRoom(id); });
  connect(leave_, &QPushButton::clicked, model_, &ClientModel::leaveRoom);
  connect(send_, &QPushButton::clicked, this, [this] { const auto text=composer_->toPlainText(); const auto size=text.toUtf8().size(); if(size>0 && size<=4096){ model_->sendChat(text); composer_->clear(); }});
  connect(model_, &ClientModel::stateChanged, this, [this] { updateState(); });
  connect(model_, &ClientModel::membersChanged, this, [this](const QStringList& values) { members_->clear(); members_->addItems(values); });
  connect(model_, &ClientModel::chatReceived, this, [this](const QString& sender, const QString& text) { timeline_->append(sender.toHtmlEscaped() + ": " + text.toHtmlEscaped()); });
  connect(model_, &ClientModel::statusMessage, this,
          [this](const QString& message) { statusBar()->showMessage(message); });
  resize(900, 600); updateState();
}

void MainWindow::updateState() {
  const auto state = model_->state(); const bool disconnected = state == ClientModel::State::Disconnected;
  const bool identified = state == ClientModel::State::Identified; const bool in_room = state == ClientModel::State::InRoom;
  connect_->setText(disconnected ? tr("Connect") : tr("Disconnect")); host_->setEnabled(disconnected); port_->setEnabled(disconnected); nickname_->setEnabled(disconnected);
  create_->setEnabled(identified); join_->setEnabled(identified); leave_->setEnabled(in_room); send_->setEnabled(in_room); composer_->setEnabled(in_room);
}

}  // namespace chatroom::qt_client
