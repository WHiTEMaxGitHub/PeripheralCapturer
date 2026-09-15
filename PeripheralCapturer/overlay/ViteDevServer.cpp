#include "ViteDevServer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <QDir>
#include <QFile>
#include <QOverload>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <spdlog/spdlog.h>

namespace {

constexpr char kHost[] = "127.0.0.1";

bool ensureWsa() {
    static int state = 0; // 0 未试，1 成功，-1 失败
    if (state != 0) {
        return state > 0;
    }
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        spdlog::warn("[pov] WSAStartup failed err={}", WSAGetLastError());
        state = -1;
        return false;
    }
    state = 1;
    return true;
}

bool portOpen(quint16 port) {
    if (!ensureWsa()) {
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return false;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, kHost, &addr.sin_addr);
    connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(s, &writeSet);
    timeval tv{};
    tv.tv_usec = 150000;
    const int sel = select(0, nullptr, &writeSet, nullptr, &tv);
    bool ok = false;
    if (sel > 0) {
        int err = 0;
        int len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        ok = (err == 0);
    }
    closesocket(s);
    return ok;
}

QString webSourceDir() {
#ifdef PC_WEB_SOURCE_DIR
    return QString::fromUtf8(PC_WEB_SOURCE_DIR);
#else
    return {};
#endif
}

QString findNpmCmd() {
    QProcess where;
    where.setProcessChannelMode(QProcess::SeparateChannels);
    where.start(QStringLiteral("where"), {QStringLiteral("npm.cmd")});
    if (!where.waitForFinished(3000) || where.exitCode() != 0) {
        return {};
    }
    const QString line = QString::fromLocal8Bit(where.readAllStandardOutput()).trimmed();
    const QString first = line.split(QLatin1Char('\n')).constFirst().trimmed();
    return first;
}

} // namespace

ViteDevServer::ViteDevServer(QObject* parent): QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&pollTimer_, &QTimer::timeout, this, &ViteDevServer::poll);
    connect(&process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
        if (!finished_) {
            spdlog::warn("[pov] Vite process exited code={} before :{} was up", code, kPort);
            finish(false);
        }
    });
    connect(&process_, &QProcess::readyRead, this, [this] {
        // 不读管道会堵死 npm/node。逐行 debug，安装日志很多不要打 info。
        const QByteArray chunk = process_.readAll().trimmed();
        if (!chunk.isEmpty()) {
            spdlog::debug("[pov] vite: {}", chunk.toStdString());
        }
    });
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
}

ViteDevServer::~ViteDevServer() {
    stop();
}

void ViteDevServer::start() {
#ifdef NDEBUG
    Q_UNUSED(this);
    return;
#else
    if (finished_ || pollTimer_.isActive() || spawned_) {
        return;
    }
    if (portOpen(kPort)) {
        spdlog::info("[pov] Vite already listening {}:{}", kHost, kPort);
        QTimer::singleShot(0, this, [this] { finish(true); });
        return;
    }
    spawn();
#endif
}

void ViteDevServer::stop() {
    pollTimer_.stop();
    if (!spawned_) {
        if (job_) {
            CloseHandle(static_cast<HANDLE>(job_));
            job_ = nullptr;
        }
        return;
    }
    spdlog::info("[pov] stopping Vite pid={}", pid_);
    killTree();
    spawned_ = false;
}

void ViteDevServer::spawn() {
    const QString web = webSourceDir();
    if (web.isEmpty() || !QDir(web).exists()) {
        spdlog::warn("[pov] web source dir missing: {}", web.toStdString());
        QTimer::singleShot(0, this, [this] { finish(false); });
        return;
    }
    const QString npm = findNpmCmd();
    if (npm.isEmpty()) {
        spdlog::warn("[pov] npm.cmd not on PATH, install Node.js or start Vite yourself");
        QTimer::singleShot(0, this, [this] { finish(false); });
        return;
    }

    const bool needInstall = !QFile::exists(web + QStringLiteral("/node_modules/vite"));
    // 没 node_modules 时先 install，POV 等到端口开再 Navigate，避免 5173 立刻失败落到离线页。
    const QString script = needInstall ? QStringLiteral("npm install && npm run dev")
                                       : QStringLiteral("npm run dev");
    maxPolls_ = needInstall ? 720 : 80; // 250ms * 720 ≈ 3min；已装依赖约 20s
    polls_ = 0;

    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(static_cast<HANDLE>(job_), JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits))) {
            spdlog::warn("[pov] SetInformationJobObject failed err={}", GetLastError());
            CloseHandle(static_cast<HANDLE>(job_));
            job_ = nullptr;
        }
    }

    process_.setWorkingDirectory(web);
    process_.setProgram(QStringLiteral("cmd.exe"));
    process_.setArguments({QStringLiteral("/d"), QStringLiteral("/s"), QStringLiteral("/c"), script});
    process_.start();
    if (!process_.waitForStarted(5000)) {
        spdlog::warn("[pov] Vite spawn failed: {}", process_.errorString().toStdString());
        QTimer::singleShot(0, this, [this] { finish(false); });
        return;
    }
    pid_ = process_.processId();
    spawned_ = true;
    if (!assignJob(pid_)) {
        spdlog::debug("[pov] Vite job assign skipped, stop will use taskkill /T");
    }
    spdlog::info("[pov] Vite spawned pid={} cwd={} install={} npm={}", pid_, web.toStdString(),
                 needInstall, npm.toStdString());
    pollTimer_.start(250);
}

void ViteDevServer::poll() {
    ++polls_;
    if (portOpen(kPort)) {
        spdlog::info("[pov] Vite ready {}:{}", kHost, kPort);
        pollTimer_.stop();
        finish(true);
        return;
    }
    if (polls_ >= maxPolls_) {
        spdlog::warn("[pov] Vite did not listen on :{} in time", kPort);
        pollTimer_.stop();
        finish(false);
        return;
    }
    if (polls_ == 1 || polls_ % 20 == 0) {
        spdlog::info("[pov] waiting for Vite :{} ({}/{})", kPort, polls_, maxPolls_);
    }
}

void ViteDevServer::finish(bool ok) {
    if (finished_) {
        return;
    }
    finished_ = true;
    ok_ = ok;
    pollTimer_.stop();
    emit ready(ok);
}

bool ViteDevServer::assignJob(qint64 pid) {
    if (!job_ || pid <= 0) {
        return false;
    }
    HANDLE proc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!proc) {
        spdlog::debug("[pov] OpenProcess Vite pid={} err={}", pid, GetLastError());
        return false;
    }
    const BOOL ok = AssignProcessToJobObject(static_cast<HANDLE>(job_), proc);
    const DWORD err = GetLastError();
    CloseHandle(proc);
    if (!ok) {
        // VS 调试器常把 exe 放进不允许嵌套的 Job，Assign 会失败。
        spdlog::debug("[pov] AssignProcessToJobObject failed err={}", err);
        CloseHandle(static_cast<HANDLE>(job_));
        job_ = nullptr;
        return false;
    }
    return true;
}

void ViteDevServer::killTree() {
    if (job_) {
        CloseHandle(static_cast<HANDLE>(job_));
        job_ = nullptr;
        if (process_.state() != QProcess::NotRunning) {
            process_.waitForFinished(3000);
        }
        if (process_.state() == QProcess::NotRunning) {
            return;
        }
    }
    if (pid_ > 0) {
        QProcess killer;
        killer.start(QStringLiteral("taskkill"),
                     {QStringLiteral("/PID"), QString::number(pid_), QStringLiteral("/T"),
                      QStringLiteral("/F")});
        killer.waitForFinished(5000);
    }
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(2000);
    }
    pid_ = 0;
}
