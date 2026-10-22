#include <QApplication>
#include <QFile>
#include <QStyleFactory>

#include "qt_client/chat_transport.h"
#include "qt_client/client_model.h"
#include "qt_client/main_window.h"

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  QApplication::setApplicationName(QStringLiteral("ChatRoom"));
  QApplication::setApplicationDisplayName(QStringLiteral("ChatRoom"));
  QApplication::setOrganizationName(QStringLiteral("ChatRoom"));
  if (auto* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
    QApplication::setStyle(fusion);
  }
  QFile stylesheet(QStringLiteral(":/styles.qss"));
  if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
    application.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
  }
  chatroom::qt_client::ChatTransport transport;
  chatroom::qt_client::ClientModel model(&transport);
  chatroom::qt_client::MainWindow window(&model);
  window.show();
  return application.exec();
}
