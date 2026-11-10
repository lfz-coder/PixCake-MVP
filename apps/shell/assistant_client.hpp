#pragma once

#include "pixcake/assistant/assistant_events.hpp"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <vector>

namespace pixcake::shell {

// Qt-side transport for the optional Python sidecar. It owns no edit state:
// events are forwarded to the caller, while C++ commands remain behind the
// AssistantGateway.
class AssistantClient final {
 public:
  struct Options {
    // LangGraph's run endpoint is thread scoped. `{thread_id}` is replaced
    // from the protocol request before the POST is sent.
    QUrl stream_url = QUrl(QStringLiteral("http://127.0.0.1:27461/threads/{thread_id}/runs/stream"));
    QString assistant_id = QStringLiteral("assistant");
    QString sidecar_executable;
    QStringList sidecar_arguments;
    QString sidecar_working_directory;
    // When the shell owns the local listener, pass its path to the child so
    // the Python gateway cannot accidentally fall back to an unrelated HTTP
    // endpoint.
    QString gateway_socket;
    int startup_timeout_ms = 10'000;
  };

  using EventHandler = std::function<void(const assistant::AssistantEvent&)>;
  using ErrorHandler = std::function<void(const QString&)>;

  explicit AssistantClient(QObject* parent = nullptr);
  ~AssistantClient();

  AssistantClient(const AssistantClient&) = delete;
  AssistantClient& operator=(const AssistantClient&) = delete;

  // Starts an explicitly configured sidecar. An empty executable means that
  // the caller manages the server process and this method is a no-op success.
  bool start_sidecar(const Options& options, QString& diagnostic);
  void stop_sidecar();
  bool sidecar_running() const;

  // Posts one LangGraph-compatible JSON request and incrementally forwards
  // SSE events. There is at most one active assistant stream per client.
  bool stream(const Options& options, const QJsonObject& request, EventHandler on_event,
              ErrorHandler on_error, QString& diagnostic);
  void cancel();
  bool busy() const { return reply_ != nullptr; }

 private:
  void fail_stream(const QString& diagnostic);
  void finish_stream();
  static QUrl stream_url_for(const QUrl& template_url, const QJsonObject& request);

  QNetworkAccessManager network_;
  QProcess sidecar_;
  QNetworkReply* reply_ = nullptr;
  assistant::AssistantSseParser parser_;
  EventHandler on_event_;
  ErrorHandler on_error_;
  bool stream_failed_ = false;
};

}  // namespace pixcake::shell
