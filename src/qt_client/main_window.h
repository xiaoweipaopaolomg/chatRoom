#pragma once

#include <QMainWindow>
#include <QStringList>

class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace chatroom::qt_client {
class ClientModel;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(ClientModel* model, QWidget* parent = nullptr);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void updateState();
  void updateRoomHeader();
  void updateMembers(const QStringList& values);
  void updateCharacterCount();
  void sendMessage();
  void addMessage(const QString& sender, const QString& text);
  void clearConversation();
  void showStatus(const QString& message, bool error = false);

  ClientModel* model_;
  QLineEdit* host_;
  QLineEdit* port_;
  QLineEdit* nickname_;
  QLineEdit* room_name_;
  QLineEdit* room_id_;
  QPushButton* connect_;
  QPushButton* create_;
  QPushButton* join_;
  QPushButton* leave_;
  QPushButton* send_;
  QListWidget* members_;
  QListWidget* timeline_;
  QPlainTextEdit* composer_;
  QStackedWidget* conversation_stack_;
  QLabel* connection_badge_;
  QLabel* room_title_;
  QLabel* room_subtitle_;
  QLabel* member_count_;
  QLabel* character_count_;
};

}  // namespace chatroom::qt_client
