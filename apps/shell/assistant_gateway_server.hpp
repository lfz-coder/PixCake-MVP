#pragma once

#include "pixcake/assistant/assistant_gateway.hpp"

#include <QLocalServer>
#include <QLocalSocket>

#include <QHash>
#include <QString>

namespace pixcake::shell {

// Main-process adapter for the sidecar's local JSON framing. The socket is
// user-only and every command is dispatched on the Qt event-loop thread before
// the reply is written back.
class AssistantGatewayServer final {
 public:
  AssistantGatewayServer(assistant::AssistantGateway& gateway, QObject* owner = nullptr);
  ~AssistantGatewayServer();

  AssistantGatewayServer(const AssistantGatewayServer&) = delete;
  AssistantGatewayServer& operator=(const AssistantGatewayServer&) = delete;

  bool start(const QString& socket_path, QString& diagnostic);
  void stop();
  bool running() const { return server_.isListening(); }
  QString socket_path() const { return socket_path_; }

 private:
  void accept_connections();
  void read_socket(QLocalSocket* socket);
  void remove_socket(QLocalSocket* socket);
  void handle_payload(QLocalSocket* socket, const QByteArray& payload);
  void send_reply(QLocalSocket* socket, const QByteArray& payload);
  void close_socket(QLocalSocket* socket);

  assistant::AssistantGateway& gateway_;
  QLocalServer server_;
  QString socket_path_;
  QHash<QLocalSocket*, QByteArray> buffers_;
};

}  // namespace pixcake::shell
