#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <cstdint>

// Debug 下拉起 PeripheralCapturer/web 的 Vite。已有 5173 则复用，退出不杀。
class ViteDevServer : public QObject {
    Q_OBJECT

public:
    static constexpr quint16 kPort = 5173;

    explicit ViteDevServer(QObject* parent = nullptr);
    ~ViteDevServer() override;

    void start();
    void stop();

signals:
    void ready(bool ok);

private:
    void spawn();
    void poll();
    void finish(bool ok);
    bool assignJob(qint64 pid);
    void killTree();

    QProcess process_;
    QTimer pollTimer_;
    void* job_ = nullptr; // HANDLE，头文件不拉 Windows.h
    qint64 pid_ = 0;
    int polls_ = 0;
    int maxPolls_ = 80;
    bool spawned_ = false;
    bool finished_ = false;
};
