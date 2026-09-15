#include "OverlayHub.h"

#include "../Input/InputEvent.h"
#include "../Input/QueuePresets.h"
#include "../Input/Timer.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>

#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>

namespace {

const char* kDefaultAxes[] = {"pad-lt", "pad-rt", "pad-lx", "pad-ly", "pad-rx", "pad-ry"};

QString makeToken() {
    return QUuid::createUuid().toString(QUuid::Id128);
}

} // namespace

OverlayServer::OverlayServer(QString token, QObject* parent)
    : QObject(parent), token_(std::move(token)), guiThread_(QThread::currentThread()) {}

QString OverlayServer::clientUrl() const {
    std::lock_guard lock(mutex_);
    return clientUrl_;
}

quint16 OverlayServer::port() const {
    std::lock_guard lock(mutex_);
    return port_;
}

void OverlayServer::postSnapshot(QByteArray json) {
    {
        std::lock_guard lock(mutex_);
        latest_ = std::move(json);
        dirty_ = true;
    }
    bool expected = false;
    if (flushQueued_.compare_exchange_strong(expected, true)) {
        QMetaObject::invokeMethod(this, &OverlayServer::flushLatest, Qt::QueuedConnection);
    }
}

bool OverlayServer::listen() {
    if (server_) {
        return server_->isListening();
    }
    server_ = new QWebSocketServer(QStringLiteral("pc-overlay"), QWebSocketServer::NonSecureMode,
                                   this);
    connect(server_, &QWebSocketServer::newConnection, this, &OverlayServer::onNewConnection);
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        spdlog::error("[pov] overlay ws listen failed: {}", server_->errorString().toStdString());
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        port_ = server_->serverPort();
        clientUrl_ = QStringLiteral("ws://127.0.0.1:%1/?token=%2").arg(port_).arg(token_);
    }
    spdlog::info("[pov] overlay ws listen 127.0.0.1:{}", server_->serverPort());
    return true;
}

void OverlayServer::shutdown() {
    if (server_) {
        server_->close();
    }
    for (auto* sock : clients_) {
        sock->close();
        sock->deleteLater();
    }
    clients_.clear();
    if (guiThread_) {
        moveToThread(guiThread_);
    }
}

void OverlayServer::onNewConnection() {
    if (!server_) {
        return;
    }
    QWebSocket* sock = server_->nextPendingConnection();
    if (!sock) {
        return;
    }
    if (socketToken(sock) != token_) {
        spdlog::warn("[pov] overlay ws reject (bad token)");
        sock->close();
        sock->deleteLater();
        return;
    }
    connect(sock, &QWebSocket::disconnected, this, &OverlayServer::onSocketDisconnected);
    clients_.push_back(sock);
    spdlog::info("[pov] overlay ws client connected");
    bool expected = false;
    if (flushQueued_.compare_exchange_strong(expected, true)) {
        flushLatest();
    }
}

void OverlayServer::onSocketDisconnected() {
    auto* sock = qobject_cast<QWebSocket*>(sender());
    if (!sock) {
        return;
    }
    clients_.removeAll(sock);
    sock->deleteLater();
    spdlog::info("[pov] overlay ws client disconnected");
}

void OverlayServer::flushLatest() {
    flushQueued_.store(false, std::memory_order_relaxed);
    QByteArray payload;
    {
        std::lock_guard lock(mutex_);
        if (!dirty_) {
            return;
        }
        payload = latest_;
        dirty_ = false;
    }
    if (payload.isEmpty() || clients_.isEmpty()) {
        return;
    }
    const QString text = QString::fromUtf8(payload);
    for (auto* sock : clients_) {
        if (sock->state() == QAbstractSocket::ConnectedState) {
            sock->sendTextMessage(text);
        }
    }
}

QString OverlayServer::socketToken(QWebSocket* socket) const {
    QString token = QUrlQuery(socket->requestUrl()).queryItemValue(QStringLiteral("token"));
    if (token.isEmpty()) {
        const QUrl viaPath(QStringLiteral("ws://local") + socket->resourceName());
        token = QUrlQuery(viaPath).queryItemValue(QStringLiteral("token"));
    }
    return token;
}

OverlayHub::OverlayHub(InputEventBus& bus) : token_(makeToken()) {
    queue_ = bus.subscribe(overlaySubscribeOptions());
    for (const char* id : kDefaultAxes) {
        axes_[id] = 0.f;
    }
    server_ = new OverlayServer(token_);
}

OverlayHub::~OverlayHub() {
    stop();
    delete server_;
    server_ = nullptr;
}

bool OverlayHub::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return true;
    }
    server_->moveToThread(&wsThread_);
    wsThread_.start();
    bool ok = false;
    if (!QMetaObject::invokeMethod(server_, "listen", Qt::BlockingQueuedConnection,
                                   Q_RETURN_ARG(bool, ok)) ||
        !ok) {
        spdlog::error("[pov] overlay ws start failed");
        wsThread_.quit();
        wsThread_.wait();
        server_->moveToThread(QThread::currentThread());
        return false;
    }
    running_.store(true, std::memory_order_relaxed);
    aggregator_ = std::thread([this] { runAggregator(); });
    return true;
}

void OverlayHub::stop() {
    running_.store(false, std::memory_order_relaxed);
    if (aggregator_.joinable()) {
        aggregator_.join();
    }
    if (wsThread_.isRunning()) {
        QMetaObject::invokeMethod(server_, "shutdown", Qt::BlockingQueuedConnection);
        wsThread_.quit();
        wsThread_.wait();
    }
}

QString OverlayHub::clientUrl() const {
    return server_ ? server_->clientUrl() : QString();
}

void OverlayHub::runAggregator() {
    spdlog::info("[pov] overlay aggregator ready");
    lastEmitUs_ = Timer::nowUs();
    dirty_ = true;
    server_->postSnapshot(packSnapshot());
    dirty_ = false;

    while (running_.load(std::memory_order_relaxed)) {
        if (auto event = queue_->popFor(std::chrono::milliseconds(16))) {
            applyEvent(*event);
            dirty_ = true;
        }
        while (auto more = queue_->tryPop()) {
            applyEvent(*more);
            dirty_ = true;
        }
        if (!dirty_) {
            continue;
        }
        const int64_t now = Timer::nowUs();
        const int fps = std::max(1, Timer::sessionFps());
        const int64_t minUs = 1'000'000 / fps;
        if (now - lastEmitUs_ < minUs) {
            continue;
        }
        server_->postSnapshot(packSnapshot());
        lastEmitUs_ = now;
        dirty_ = false;
    }
}

void OverlayHub::applyEvent(const InputEvent& event) {
    lastTimestampUs_ = event.timestampUs;
    if (event.control.empty()) {
        return;
    }
    switch (event.type) {
    case InputEventType::KeyDown:
    case InputEventType::MouseButtonDown:
    case InputEventType::ButtonDown:
        keys_.insert(event.control);
        break;
    case InputEventType::KeyUp:
    case InputEventType::MouseButtonUp:
    case InputEventType::ButtonUp:
        keys_.erase(event.control);
        break;
    case InputEventType::AxisChanged:
        axes_[event.control] = event.normalizedValue;
        break;
    default:
        break;
    }
}

QByteArray OverlayHub::packSnapshot() const {
    QJsonObject payload;
    const int fps = std::max(1, Timer::sessionFps());
    payload.insert(QStringLiteral("frameIndex"),
                   static_cast<int>(Timer::ToFrameIndex(lastTimestampUs_, fps)));

    std::vector<std::string> sorted(keys_.begin(), keys_.end());
    std::sort(sorted.begin(), sorted.end());
    QJsonArray keys;
    for (const auto& id : sorted) {
        keys.append(QString::fromStdString(id));
    }
    payload.insert(QStringLiteral("keys"), keys);

    QJsonObject axes;
    for (const auto& [id, value] : axes_) {
        axes.insert(QString::fromStdString(id), static_cast<double>(value));
    }
    payload.insert(QStringLiteral("axes"), axes);
    payload.insert(QStringLiteral("mouseDx"), 0);
    payload.insert(QStringLiteral("mouseDy"), 0);

    QJsonObject root;
    root.insert(QStringLiteral("type"), QStringLiteral("snapshot"));
    root.insert(QStringLiteral("payload"), payload);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
