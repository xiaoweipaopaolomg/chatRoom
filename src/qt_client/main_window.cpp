#include "qt_client/main_window.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <utility>

#include "protocol/limits.h"
#include "qt_client/client_model.h"

namespace chatroom::qt_client {
namespace {

constexpr int kSidebarWidth = 330;
constexpr int kBubbleMaximumWidth = 560;

QLabel* MakeLabel(const QString& text, const char* object_name, QWidget* parent = nullptr) {
  auto* label = new QLabel(text, parent);
  label->setObjectName(QString::fromLatin1(object_name));
  return label;
}

QFrame* MakeDivider(QWidget* parent = nullptr) {
  auto* divider = new QFrame(parent);
  divider->setObjectName(QStringLiteral("divider"));
  divider->setFrameShape(QFrame::HLine);
  divider->setFixedHeight(1);
  return divider;
}

QColor AvatarColor(const QString& name) {
  static constexpr std::array<const char*, 8> colors{
      "#4f8fe8", "#7b61d1", "#d46f7f", "#e08b45",
      "#42a38b", "#397db8", "#a06bc1", "#5b9a55"};
  return QColor(QString::fromLatin1(colors[qHash(name) % colors.size()]));
}

QPixmap Avatar(const QString& name, int logical_size, qreal device_ratio) {
  const auto pixel_size = static_cast<int>(logical_size * device_ratio);
  QPixmap image(pixel_size, pixel_size);
  image.setDevicePixelRatio(device_ratio);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(AvatarColor(name));
  painter.drawEllipse(QRectF(0, 0, logical_size, logical_size));
  painter.setPen(Qt::white);
  auto font = painter.font();
  font.setBold(true);
  font.setPixelSize(static_cast<int>(logical_size * 0.42));
  painter.setFont(font);
  const auto initial = name.trimmed().left(1).toUpper();
  painter.drawText(QRectF(0, 0, logical_size, logical_size), Qt::AlignCenter,
                   initial.isEmpty() ? QStringLiteral("?") : initial);
  return image;
}

QWidget* MakeMessageBubble(const QString& sender, const QString& text, bool outgoing,
                           QWidget* parent = nullptr) {
  auto* row = new QWidget(parent);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(14, 2, 14, 2);
  row_layout->setSpacing(8);

  auto* bubble = new QFrame(row);
  bubble->setObjectName(outgoing ? QStringLiteral("outgoingBubble")
                                 : QStringLiteral("incomingBubble"));
  bubble->setMaximumWidth(kBubbleMaximumWidth);
  auto* bubble_layout = new QVBoxLayout(bubble);
  bubble_layout->setContentsMargins(13, 9, 13, 8);
  bubble_layout->setSpacing(3);

  if (!outgoing) {
    auto* sender_label = MakeLabel(sender, "messageSender", bubble);
    sender_label->setStyleSheet(QStringLiteral("color: %1;").arg(AvatarColor(sender).name()));
    bubble_layout->addWidget(sender_label);
  }
  auto* body = MakeLabel(text, "messageBody", bubble);
  body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
  body->setWordWrap(true);
  body->setMaximumWidth(kBubbleMaximumWidth - 26);
  bubble_layout->addWidget(body);
  auto* time = MakeLabel(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")),
                         "messageTime", bubble);
  time->setAlignment(Qt::AlignRight);
  bubble_layout->addWidget(time);

  if (outgoing) {
    row_layout->addStretch(1);
    row_layout->addWidget(bubble, 0, Qt::AlignRight);
  } else {
    row_layout->addWidget(bubble, 0, Qt::AlignLeft);
    row_layout->addStretch(1);
  }
  return row;
}

void RefreshDynamicStyle(QWidget* widget) {
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

}  // namespace

MainWindow::MainWindow(ClientModel* model, QWidget* parent)
    : QMainWindow(parent), model_(model) {
  setObjectName(QStringLiteral("mainWindow"));
  setWindowTitle(tr("ChatRoom — C++17 Messenger"));
  setMinimumSize(900, 620);
  resize(1180, 760);

  auto* central = new QWidget(this);
  central->setObjectName(QStringLiteral("central"));
  auto* root = new QHBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  auto* splitter = new QSplitter(Qt::Horizontal, central);
  splitter->setObjectName(QStringLiteral("mainSplitter"));
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(1);
  root->addWidget(splitter);

  auto* sidebar = new QFrame(splitter);
  sidebar->setObjectName(QStringLiteral("sidebar"));
  sidebar->setMinimumWidth(kSidebarWidth);
  sidebar->setMaximumWidth(390);
  auto* sidebar_layout = new QVBoxLayout(sidebar);
  sidebar_layout->setContentsMargins(20, 20, 20, 18);
  sidebar_layout->setSpacing(14);

  auto* brand_row = new QHBoxLayout;
  brand_row->setSpacing(11);
  auto* brand_mark = MakeLabel(QStringLiteral("C"), "brandMark", sidebar);
  brand_mark->setAlignment(Qt::AlignCenter);
  brand_mark->setFixedSize(44, 44);
  auto* brand_text = new QVBoxLayout;
  brand_text->setSpacing(0);
  brand_text->addWidget(MakeLabel(tr("ChatRoom"), "brandTitle", sidebar));
  brand_text->addWidget(MakeLabel(tr("Fast · durable · private"), "brandSubtitle", sidebar));
  brand_row->addWidget(brand_mark);
  brand_row->addLayout(brand_text, 1);
  connection_badge_ = MakeLabel(tr("Offline"), "connectionBadge", sidebar);
  connection_badge_->setProperty("connectionState", "offline");
  connection_badge_->setAlignment(Qt::AlignCenter);
  brand_row->addWidget(connection_badge_);
  sidebar_layout->addLayout(brand_row);

  auto* connection_card = new QFrame(sidebar);
  connection_card->setObjectName(QStringLiteral("sideCard"));
  auto* connection_layout = new QVBoxLayout(connection_card);
  connection_layout->setContentsMargins(14, 14, 14, 14);
  connection_layout->setSpacing(9);
  connection_layout->addWidget(MakeLabel(tr("CONNECTION"), "sectionLabel", connection_card));
  auto* endpoint_row = new QHBoxLayout;
  endpoint_row->setSpacing(8);
  host_ = new QLineEdit(QStringLiteral("127.0.0.1"), connection_card);
  host_->setObjectName(QStringLiteral("hostEdit"));
  host_->setPlaceholderText(tr("Server address"));
  port_ = new QLineEdit(QStringLiteral("7777"), connection_card);
  port_->setObjectName(QStringLiteral("portEdit"));
  port_->setPlaceholderText(tr("Port"));
  port_->setMaximumWidth(78);
  endpoint_row->addWidget(host_, 1);
  endpoint_row->addWidget(port_);
  connection_layout->addLayout(endpoint_row);
  nickname_ = new QLineEdit(connection_card);
  nickname_->setObjectName(QStringLiteral("nicknameEdit"));
  nickname_->setPlaceholderText(tr("Your display name"));
  nickname_->setMaxLength(static_cast<int>(protocol::kMaximumNicknameBytes));
  connection_layout->addWidget(nickname_);
  connect_ = new QPushButton(tr("Connect"), connection_card);
  connect_->setObjectName(QStringLiteral("connectButton"));
  connect_->setCursor(Qt::PointingHandCursor);
  connection_layout->addWidget(connect_);
  sidebar_layout->addWidget(connection_card);

  sidebar_layout->addWidget(MakeLabel(tr("ROOMS"), "sectionLabel", sidebar));
  auto* create_row = new QHBoxLayout;
  create_row->setSpacing(8);
  room_name_ = new QLineEdit(sidebar);
  room_name_->setPlaceholderText(tr("Create a room"));
  room_name_->setObjectName(QStringLiteral("roomNameEdit"));
  room_name_->setMaxLength(static_cast<int>(protocol::kMaximumRoomNameBytes));
  create_ = new QPushButton(tr("Create"), sidebar);
  create_->setObjectName(QStringLiteral("createButton"));
  create_->setProperty("buttonRole", "secondary");
  create_->setCursor(Qt::PointingHandCursor);
  create_row->addWidget(room_name_, 1);
  create_row->addWidget(create_);
  sidebar_layout->addLayout(create_row);

  auto* join_row = new QHBoxLayout;
  join_row->setSpacing(8);
  room_id_ = new QLineEdit(sidebar);
  room_id_->setPlaceholderText(tr("Join by room ID"));
  room_id_->setObjectName(QStringLiteral("roomIdEdit"));
  join_ = new QPushButton(tr("Join"), sidebar);
  join_->setObjectName(QStringLiteral("joinButton"));
  join_->setProperty("buttonRole", "secondary");
  join_->setCursor(Qt::PointingHandCursor);
  join_row->addWidget(room_id_, 1);
  join_row->addWidget(join_);
  sidebar_layout->addLayout(join_row);
  sidebar_layout->addWidget(MakeDivider(sidebar));

  auto* members_header = new QHBoxLayout;
  members_header->addWidget(MakeLabel(tr("MEMBERS"), "sectionLabel", sidebar));
  members_header->addStretch(1);
  member_count_ = MakeLabel(QStringLiteral("0"), "memberCount", sidebar);
  member_count_->setAlignment(Qt::AlignCenter);
  members_header->addWidget(member_count_);
  sidebar_layout->addLayout(members_header);
  members_ = new QListWidget(sidebar);
  members_->setObjectName(QStringLiteral("memberList"));
  members_->setFrameShape(QFrame::NoFrame);
  members_->setFocusPolicy(Qt::NoFocus);
  members_->setSelectionMode(QAbstractItemView::NoSelection);
  sidebar_layout->addWidget(members_, 1);

  auto* chat = new QFrame(splitter);
  chat->setObjectName(QStringLiteral("chatPanel"));
  auto* chat_layout = new QVBoxLayout(chat);
  chat_layout->setContentsMargins(0, 0, 0, 0);
  chat_layout->setSpacing(0);
  auto* chat_header = new QFrame(chat);
  chat_header->setObjectName(QStringLiteral("chatHeader"));
  auto* chat_header_layout = new QHBoxLayout(chat_header);
  chat_header_layout->setContentsMargins(22, 13, 18, 13);
  chat_header_layout->setSpacing(12);
  auto* room_mark = MakeLabel(QStringLiteral("#"), "roomMark", chat_header);
  room_mark->setAlignment(Qt::AlignCenter);
  room_mark->setFixedSize(44, 44);
  chat_header_layout->addWidget(room_mark);
  auto* room_labels = new QVBoxLayout;
  room_labels->setSpacing(1);
  room_title_ = MakeLabel(tr("No room selected"), "roomTitle", chat_header);
  room_subtitle_ = MakeLabel(tr("Connect and join a room to start chatting"),
                             "roomSubtitle", chat_header);
  room_labels->addWidget(room_title_);
  room_labels->addWidget(room_subtitle_);
  chat_header_layout->addLayout(room_labels, 1);
  leave_ = new QPushButton(tr("Leave room"), chat_header);
  leave_->setObjectName(QStringLiteral("leaveButton"));
  leave_->setProperty("buttonRole", "ghost");
  leave_->setCursor(Qt::PointingHandCursor);
  chat_header_layout->addWidget(leave_);
  chat_layout->addWidget(chat_header);

  conversation_stack_ = new QStackedWidget(chat);
  conversation_stack_->setObjectName(QStringLiteral("conversationStack"));
  auto* empty_state = new QWidget(conversation_stack_);
  empty_state->setObjectName(QStringLiteral("emptyState"));
  auto* empty_layout = new QVBoxLayout(empty_state);
  empty_layout->setAlignment(Qt::AlignCenter);
  auto* empty_icon = MakeLabel(QStringLiteral("✦"), "emptyIcon", empty_state);
  empty_icon->setAlignment(Qt::AlignCenter);
  empty_icon->setFixedSize(74, 74);
  empty_layout->addWidget(empty_icon, 0, Qt::AlignCenter);
  empty_layout->addSpacing(8);
  empty_layout->addWidget(MakeLabel(tr("Your messages live here"), "emptyTitle", empty_state),
                          0, Qt::AlignCenter);
  auto* empty_hint = MakeLabel(tr("Pick a room on the left, then say hello."),
                               "emptyHint", empty_state);
  empty_hint->setAlignment(Qt::AlignCenter);
  empty_layout->addWidget(empty_hint, 0, Qt::AlignCenter);
  conversation_stack_->addWidget(empty_state);
  timeline_ = new QListWidget(conversation_stack_);
  timeline_->setObjectName(QStringLiteral("timeline"));
  timeline_->setFrameShape(QFrame::NoFrame);
  timeline_->setFocusPolicy(Qt::NoFocus);
  timeline_->setSelectionMode(QAbstractItemView::NoSelection);
  timeline_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  timeline_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  conversation_stack_->addWidget(timeline_);
  chat_layout->addWidget(conversation_stack_, 1);

  auto* composer_frame = new QFrame(chat);
  composer_frame->setObjectName(QStringLiteral("composerFrame"));
  auto* composer_layout = new QHBoxLayout(composer_frame);
  composer_layout->setContentsMargins(20, 12, 18, 14);
  composer_layout->setSpacing(11);
  auto* editor_shell = new QFrame(composer_frame);
  editor_shell->setObjectName(QStringLiteral("editorShell"));
  auto* editor_layout = new QVBoxLayout(editor_shell);
  editor_layout->setContentsMargins(14, 7, 10, 5);
  editor_layout->setSpacing(0);
  composer_ = new QPlainTextEdit(editor_shell);
  composer_->setObjectName(QStringLiteral("messageEdit"));
  composer_->setPlaceholderText(tr("Write a message…"));
  composer_->setMaximumHeight(92);
  composer_->setTabChangesFocus(true);
  composer_->installEventFilter(this);
  editor_layout->addWidget(composer_);
  character_count_ = MakeLabel(QStringLiteral("0 / 4096"), "characterCount", editor_shell);
  character_count_->setAlignment(Qt::AlignRight);
  editor_layout->addWidget(character_count_);
  composer_layout->addWidget(editor_shell, 1);
  send_ = new QPushButton(QStringLiteral("➤"), composer_frame);
  send_->setObjectName(QStringLiteral("sendButton"));
  send_->setAccessibleName(tr("Send message"));
  send_->setToolTip(tr("Send message (Enter)"));
  send_->setCursor(Qt::PointingHandCursor);
  send_->setFixedSize(48, 48);
  composer_layout->addWidget(send_, 0, Qt::AlignBottom);
  chat_layout->addWidget(composer_frame);

  splitter->addWidget(sidebar);
  splitter->addWidget(chat);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({kSidebarWidth, width() - kSidebarWidth});
  setCentralWidget(central);
  statusBar()->setObjectName(QStringLiteral("appStatusBar"));
  statusBar()->setSizeGripEnabled(false);
  statusBar()->showMessage(tr("Ready"));

  connect(connect_, &QPushButton::clicked, this, [this] {
    if (model_->state() != ClientModel::State::Disconnected) {
      model_->disconnectFromServer();
      return;
    }
    bool valid_port = false;
    const auto port = port_->text().toUShort(&valid_port);
    const auto nickname_bytes = nickname_->text().trimmed().toUtf8();
    if (!valid_port || nickname_bytes.isEmpty() ||
        nickname_bytes.size() > static_cast<qsizetype>(protocol::kMaximumNicknameBytes)) {
      showStatus(tr("Enter a valid port and a nickname up to 32 UTF-8 bytes."), true);
      return;
    }
    model_->connectToServer(host_->text().trimmed(), port, nickname_->text().trimmed());
    showStatus(tr("Connecting to %1:%2…").arg(host_->text(), port_->text()));
  });
  connect(create_, &QPushButton::clicked, this, [this] {
    const auto name = room_name_->text().trimmed();
    const auto bytes = name.toUtf8();
    if (bytes.isEmpty() ||
        bytes.size() > static_cast<qsizetype>(protocol::kMaximumRoomNameBytes)) {
      showStatus(tr("Room names must contain 1–64 UTF-8 bytes."), true);
      return;
    }
    model_->createRoom(name);
  });
  connect(join_, &QPushButton::clicked, this, [this] {
    bool valid = false;
    const auto id = room_id_->text().toULongLong(&valid);
    if (!valid || id == 0) {
      showStatus(tr("Enter a positive numeric room ID."), true);
      return;
    }
    model_->joinRoom(id);
  });
  connect(leave_, &QPushButton::clicked, model_, &ClientModel::leaveRoom);
  connect(send_, &QPushButton::clicked, this, &MainWindow::sendMessage);
  connect(composer_, &QPlainTextEdit::textChanged, this, &MainWindow::updateCharacterCount);
  connect(model_, &ClientModel::stateChanged, this, [this] { updateState(); });
  connect(model_, &ClientModel::roomChanged, this, [this] {
    clearConversation();
    updateRoomHeader();
  });
  connect(model_, &ClientModel::membersChanged, this, &MainWindow::updateMembers);
  connect(model_, &ClientModel::chatReceived, this, &MainWindow::addMessage);
  connect(model_, &ClientModel::statusMessage, this,
          [this](const QString& message) { showStatus(message, true); });
  connect(model_, &ClientModel::diagnosticMessage, this,
          [this](const QString& message) { showStatus(message); });

  updateMembers({});
  updateRoomHeader();
  updateCharacterCount();
  updateState();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == composer_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
        !key->modifiers().testFlag(Qt::ShiftModifier)) {
      sendMessage();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateState() {
  const auto state = model_->state();
  const bool disconnected = state == ClientModel::State::Disconnected;
  const bool identified = state == ClientModel::State::Identified;
  const bool in_room = state == ClientModel::State::InRoom;
  connect_->setText(disconnected ? tr("Connect") : tr("Disconnect"));
  connect_->setProperty("buttonRole", disconnected ? "primary" : "danger");
  RefreshDynamicStyle(connect_);
  host_->setEnabled(disconnected);
  port_->setEnabled(disconnected);
  nickname_->setEnabled(disconnected);
  create_->setEnabled(identified);
  room_name_->setEnabled(identified);
  join_->setEnabled(identified);
  room_id_->setEnabled(identified);
  leave_->setEnabled(in_room);
  send_->setEnabled(in_room);
  composer_->setEnabled(in_room);

  const auto [badge_text, badge_state] = [&]() -> std::pair<QString, QByteArray> {
    switch (state) {
      case ClientModel::State::Disconnected: return {tr("Offline"), "offline"};
      case ClientModel::State::Connected: return {tr("Signing in"), "connecting"};
      case ClientModel::State::Identified: return {tr("Online"), "online"};
      case ClientModel::State::InRoom: return {tr("In room"), "online"};
    }
    return {tr("Offline"), "offline"};
  }();
  connection_badge_->setText(badge_text);
  connection_badge_->setProperty("connectionState", badge_state);
  RefreshDynamicStyle(connection_badge_);
  updateRoomHeader();
}

void MainWindow::updateRoomHeader() {
  if (model_->state() == ClientModel::State::InRoom) {
    room_title_->setText(model_->roomName().isEmpty()
                             ? tr("Room #%1").arg(model_->roomId())
                             : model_->roomName());
    room_subtitle_->setText(tr("Room #%1 · %2 member(s)")
                                .arg(model_->roomId())
                                .arg(model_->members().size()));
    return;
  }
  room_title_->setText(tr("No room selected"));
  room_subtitle_->setText(model_->state() == ClientModel::State::Disconnected
                              ? tr("Connect to the server to get started")
                              : tr("Create a room or join with its numeric ID"));
}

void MainWindow::updateMembers(const QStringList& values) {
  members_->clear();
  const auto ratio = devicePixelRatioF();
  for (const auto& member : values) {
    auto* item = new QListWidgetItem(QIcon(Avatar(member, 34, ratio)), member, members_);
    item->setSizeHint(QSize(0, 46));
    item->setToolTip(member == model_->nickname() ? tr("%1 (you)").arg(member) : member);
  }
  member_count_->setText(QString::number(values.size()));
  updateRoomHeader();
}

void MainWindow::updateCharacterCount() {
  const auto byte_count = composer_->toPlainText().toUtf8().size();
  character_count_->setText(tr("%1 / %2 bytes")
                                .arg(byte_count)
                                .arg(protocol::kMaximumChatTextBytes));
  character_count_->setProperty(
      "limitState", byte_count > static_cast<qsizetype>(protocol::kMaximumChatTextBytes)
                        ? "over"
                        : "normal");
  RefreshDynamicStyle(character_count_);
}

void MainWindow::sendMessage() {
  const auto text = composer_->toPlainText().trimmed();
  const auto byte_count = text.toUtf8().size();
  if (model_->state() != ClientModel::State::InRoom || text.isEmpty()) return;
  if (byte_count > static_cast<qsizetype>(protocol::kMaximumChatTextBytes)) {
    showStatus(tr("Messages can contain at most 4096 UTF-8 bytes."), true);
    return;
  }
  model_->sendChat(text);
  composer_->clear();
}

void MainWindow::addMessage(const QString& sender, const QString& text) {
  auto* item = new QListWidgetItem(timeline_);
  auto* bubble = MakeMessageBubble(sender, text, sender == model_->nickname(), timeline_);
  bubble->adjustSize();
  item->setSizeHint(QSize(0, bubble->sizeHint().height()));
  timeline_->setItemWidget(item, bubble);
  conversation_stack_->setCurrentWidget(timeline_);
  QTimer::singleShot(0, timeline_, [this] {
    timeline_->verticalScrollBar()->setValue(timeline_->verticalScrollBar()->maximum());
  });
}

void MainWindow::clearConversation() {
  timeline_->clear();
  conversation_stack_->setCurrentIndex(0);
}

void MainWindow::showStatus(const QString& message, bool error) {
  statusBar()->setProperty("messageType", error ? "error" : "info");
  RefreshDynamicStyle(statusBar());
  statusBar()->showMessage(message, error ? 6000 : 3500);
}

}  // namespace chatroom::qt_client
