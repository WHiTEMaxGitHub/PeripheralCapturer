#pragma once

#include "../Input/InputEventBus.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

class QWebSocket;
class QWebSocketServer;

// 只在 WS 线程碰 QWebSocket。聚合线程用 postSnapshot 覆盖最新一帧。
class OverlayServer : public QObject {
    Q_OBJECT

public:
    explicit OverlayServer(QString token, QObject* parent = nullptr);

    QString clientUrl() const;
    quint16 port() const;

    // 聚合线程调用：只留最新 JSON，再排队 flush。
    void postSnapshot(QByteArray json);

public slots:
    bool listen();
    void shutdown();

private slots:
    void onNewConnection();
    void onSocketDisconnected();
    void flushLatest();

private:
    QString socketToken(QWebSocket* socket) const;

    QString token_;
    QThread* guiThread_ = nullptr;
    QWebSocketServer* server_ = nullptr;
    QList<QWebSocket*> clients_;

    mutable std::mutex mutex_;
    QString clientUrl_;
    quint16 port_ = 0;
    QByteArray latest_;
    bool dirty_ = false;
    std::atomic<bool> flushQueued_{false};
};

// 订 overlay 队列、聚合成 snapshot、经本机 WS 推给 POV。可丢旧帧。
class OverlayHub {
public:
    explicit OverlayHub(InputEventBus& bus);
    ~OverlayHub();

    OverlayHub(const OverlayHub&) = delete;
    OverlayHub& operator=(const OverlayHub&) = delete;

    bool start();
    void stop();

    QString clientUrl() const;

private:
    void runAggregator();
    void applyEvent(const InputEvent& event);
    QByteArray packSnapshot() const;

    std::shared_ptr<BoundedQueue<InputEvent>> queue_;
    QString token_;
    OverlayServer* server_ = nullptr;
    QThread wsThread_;
    std::thread aggregator_;
    std::atomic<bool> running_{false};

    std::unordered_set<std::string> keys_;
    std::unordered_map<std::string, float> axes_;
    int64_t lastTimestampUs_ = 0;
    int64_t lastEmitUs_ = 0;
    bool dirty_ = false;
};
