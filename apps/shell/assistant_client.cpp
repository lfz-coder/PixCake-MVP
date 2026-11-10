#include "assistant_client.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QNetworkRequest>

#include <string>
#include <utility>

namespace pixcake::shell {

AssistantClient::AssistantClient(QObject* parent) : network_(parent), sidecar_(parent) {}

AssistantClient::~AssistantClient() {
  cancel();
  stop_sidecar();
}

bool AssistantClient::start_sidecar(const Options& options, QString& diagnostic) {
  diagnostic.clear();
  if (options.sidecar_executable.isEmpty()) return true;
  if (sidecar_.state() != QProcess::NotRunning) return true;
  sidecar_.setWorkingDirectory(options.sidecar_working_directory);
  auto environment = QProcessEnvironment::systemEnvironment();
  if (options.gateway_socket.isEmpty()) {
    environment.remove(QStringLiteral("PIXCAKE_GATEWAY_SOCKET"));
  } else {
    environment.insert(QStringLiteral("PIXCAKE_GATEWAY_SOCKET"), options.gateway_socket);
  }
  sidecar_.setProcessEnvironment(environment);
  sidecar_.start(options.sidecar_executable, options.sidecar_arguments);
  if (!sidecar_.waitForStarted(options.startup_timeout_ms)) {
    diagnostic = sidecar_.errorString();
    if (diagnostic.isEmpty()) diagnostic = QStringLiteral("PixCake assistant sidecar did not start");
    return false;
  }
  return true;
}

void AssistantClient::stop_sidecar() {
  if (sidecar_.state() == QProcess::NotRunning) return;
  sidecar_.terminate();
  if (!sidecar_.waitForFinished(1500)) {
    sidecar_.kill();
    sidecar_.waitForFinished(1500);
  }
}

bool AssistantClient::sidecar_running() const { return sidecar_.state() != QProcess::NotRunning; }

QUrl AssistantClient::stream_url_for(const QUrl& template_url, const QJsonObject& request) {
  const QString thread_id = request.value(QStringLiteral("thread_id")).toString().trimmed();
  if (thread_id.isEmpty()) return template_url;
  QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(thread_id));
  QString value = template_url.toString();
  value.replace(QStringLiteral("{thread_id}"), encoded);
  return QUrl(value);
}

bool AssistantClient::stream(const Options& options, const QJsonObject& request, EventHandler on_event,
                             ErrorHandler on_error, QString& diagnostic) {
  diagnostic.clear();
  if (reply_ != nullptr) {
    diagnostic = QStringLiteral("PixCake assistant already has an active request");
    return false;
  }
  const QUrl target_url = stream_url_for(options.stream_url, request);
  if (!target_url.isValid() || target_url.scheme().isEmpty()) {
    diagnostic = QStringLiteral("PixCake assistant stream URL is invalid");
    return false;
  }
  parser_.reset();
  on_event_ = std::move(on_event);
  on_error_ = std::move(on_error);
  stream_failed_ = false;

  // Keep the application protocol inside LangGraph's standard run envelope.
  // The graph receives `input.request`; the thread id in the URL gives the
  // server durable multi-turn state without making the C++ client depend on
  // LangGraph's internal state representation.
  QJsonObject input;
  input.insert(QStringLiteral("request"), request);
  QJsonObject body;
  body.insert(QStringLiteral("assistant_id"), options.assistant_id.isEmpty() ? QStringLiteral("assistant")
                                                                              : options.assistant_id);
  body.insert(QStringLiteral("input"), input);
  body.insert(QStringLiteral("stream_mode"), QStringLiteral("updates"));

  QNetworkRequest network_request(target_url);
  network_request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  network_request.setRawHeader("Accept", "text/event-stream");
  reply_ = network_.post(network_request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  QNetworkReply* active = reply_;
  QObject::connect(active, &QNetworkReply::readyRead, active, [this, active] {
    if (reply_ != active || stream_failed_) return;
    const QByteArray bytes = active->readAll();
    std::vector<assistant::AssistantEvent> events;
    std::string parser_diagnostic;
    const ResultCode code = parser_.feed(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())), events,
                                         parser_diagnostic);
    if (code != ResultCode::Ok) {
      fail_stream(QString::fromStdString(parser_diagnostic));
      return;
    }
    for (const auto& event : events) {
      if (on_event_) on_event_(event);
    }
  });
  QObject::connect(active, &QNetworkReply::finished, active, [this, active] {
    if (reply_ != active) return;
    if (stream_failed_) {
      finish_stream();
      return;
    }
    if (active->error() != QNetworkReply::NoError) {
      fail_stream(active->errorString());
      return;
    }
    std::vector<assistant::AssistantEvent> events;
    std::string parser_diagnostic;
    const ResultCode code = parser_.finish(events, parser_diagnostic);
    if (code != ResultCode::Ok) {
      fail_stream(QString::fromStdString(parser_diagnostic));
      return;
    }
    for (const auto& event : events) {
      if (on_event_) on_event_(event);
    }
    finish_stream();
  });
  return true;
}

void AssistantClient::cancel() {
  if (reply_ == nullptr) return;
  stream_failed_ = true;
  QNetworkReply* active = reply_;
  active->abort();
  active->deleteLater();
  reply_ = nullptr;
  parser_.reset();
  on_event_ = {};
  on_error_ = {};
}

void AssistantClient::fail_stream(const QString& diagnostic) {
  if (reply_ == nullptr || stream_failed_) return;
  stream_failed_ = true;
  if (on_error_) on_error_(diagnostic.isEmpty() ? QStringLiteral("PixCake assistant stream failed") : diagnostic);
  reply_->abort();
}

void AssistantClient::finish_stream() {
  if (reply_ != nullptr) reply_->deleteLater();
  reply_ = nullptr;
  parser_.reset();
  on_event_ = {};
  on_error_ = {};
  stream_failed_ = false;
}

}  // namespace pixcake::shell
