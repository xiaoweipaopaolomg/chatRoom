#include <QApplication>
#include "qt_client/chat_transport.h"
#include "qt_client/client_model.h"
#include "qt_client/main_window.h"

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  chatroom::qt_client::ChatTransport transport;
  chatroom::qt_client::ClientModel model(&transport);
  chatroom::qt_client::MainWindow window(&model);
  window.show(); return application.exec();
}
