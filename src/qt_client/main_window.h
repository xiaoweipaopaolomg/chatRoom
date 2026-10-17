#pragma once

#include <QMainWindow>

class QLineEdit; class QPushButton; class QListWidget; class QPlainTextEdit; class QTextEdit;
namespace chatroom::qt_client {
class ClientModel;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(ClientModel* model, QWidget* parent = nullptr);
 private:
  void updateState();
  ClientModel* model_;
  QLineEdit *host_, *port_, *nickname_, *room_name_, *room_id_;
  QPushButton *connect_, *create_, *join_, *leave_, *send_;
  QListWidget* members_;
  QTextEdit* timeline_;
  QPlainTextEdit* composer_;
};
}  // namespace chatroom::qt_client
