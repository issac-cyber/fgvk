#include "process.hpp"

#include "shared/protocol.hpp"

#include <glib.h>

#include <variant>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace proto = screenfg::protocol;

namespace {

void postIdle(std::function<void()> fn) {
    auto* cap = new std::function<void()>(std::move(fn));
    g_idle_add([](void* p) -> gboolean {
        auto* f = static_cast<std::function<void()>*>(p);
        (*f)();
        delete f;
        return G_SOURCE_REMOVE;
    }, cap);
}

void writeAll(int fd, const char* b, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = write(fd, b + off, n - off);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return; // EPIPE 等
        }
        off += (size_t) r;
    }
}

// 等 fd 有資料（100ms 超時）：回 1=有資料、0=超時/可回圈檢查 stopping_、-1=錯
int pollFd(int fd) {
    struct pollfd p;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    int pr = poll(&p, 1, 100);
    if (pr < 0)
        return (errno == EINTR) ? 0 : -1;
    if (pr == 0)
        return 0;
    return 1;
}

int waitExitCode(pid_t pid) {
    int st = 0;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno == EINTR)
            continue;
        return 0; // ECHILD 等
    }
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    if (WIFSIGNALED(st))
        return 128 + WTERMSIG(st);
    return 0;
}

} // namespace

ScreenFgProcess::ScreenFgProcess() = default;

ScreenFgProcess::~ScreenFgProcess() {
    if (running_.load())
        sendCommand(proto::Cmd::Quit);
    stopping_.store(true); // reader 於 ~100ms 內退出（bounded join，不讓 app 退出卡死）
    if (stdinW_ >= 0) { close(stdinW_); stdinW_ = -1; } // 子程序 stdin EOF
    if (reader_.joinable())
        reader_.join();
    if (stderrReader_.joinable())
        stderrReader_.join();
    if (stdoutR_ >= 0) { close(stdoutR_); stdoutR_ = -1; }
    if (stderrR_ >= 0) { close(stderrR_); stderrR_ = -1; }
    if (pid_ > 0) {
        int st = 0;
        if (waitpid(pid_, &st, WNOHANG) == 0) // 子程序還活著（沒處理 quit）→ 收掉
            kill(pid_, SIGTERM);
    }
}

bool ScreenFgProcess::start(const std::string& binary, const std::vector<std::string>& extraArgs) {
    if (running_.load())
        return false;

    int stdinPipe[2], stdoutPipe[2], stderrPipe[2];
    if (pipe(stdinPipe) != 0 || pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0)
        return false;

    pid_ = fork();
    if (pid_ < 0) {
        for (auto fd : {stdinPipe[0], stdinPipe[1], stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1]})
            close(fd);
        return false;
    }

    if (pid_ == 0) {
        // 子程序
        dup2(stdinPipe[0], 0);
        dup2(stdoutPipe[1], 1);
        dup2(stderrPipe[1], 2);
        for (auto fd : {stdinPipe[0], stdinPipe[1], stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1]})
            close(fd);

        std::vector<const char*> argv;
        argv.push_back(binary.c_str());
        for (auto& a : extraArgs)
            argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execv(binary.c_str(), const_cast<char**>(argv.data()));
        _exit(127); // exec 失敗
    }

    // 父程序
    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    close(stderrPipe[1]);
    stdinW_ = stdinPipe[1];
    stdoutR_ = stdoutPipe[0];
    stderrR_ = stderrPipe[0];
    running_.store(true);
    exitReported_.store(false);
    stopping_.store(false);

    try {
        reader_ = std::thread(&ScreenFgProcess::readerLoop, this);
        stderrReader_ = std::thread(&ScreenFgProcess::logLoop, this);
    } catch (...) {
        // thread 建立失敗：殺掉子程序 + 清 fd，避免 leak
        kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
        running_.store(false);
        for (auto fd : {stdinW_, stdoutR_, stderrR_})
            if (fd >= 0)
                close(fd);
        stdinW_ = stdoutR_ = stderrR_ = -1;
        return false;
    }
    return true;
}

void ScreenFgProcess::readerLoop() {
    char chunk[8192];
    std::string pending;
    bool eof = false;
    while (!stopping_.load()) {
        int pr = pollFd(stdoutR_);
        if (pr < 0)
            break;
        if (pr == 0)
            continue; // 超時 → 回圈頂檢查 stopping_
        ssize_t r = read(stdoutR_, chunk, sizeof chunk);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (r == 0) { eof = true; break; }
        pending.append(chunk, (size_t) r);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            // 解碼交給 shared ProtocolDecoder（純 parse）；transport 只負責 IO + 回調
            auto msg = proto::parse(line);
            if (!msg)
                continue; // 缺欄 / 格式錯 → 明確跳過（不默默 default）
            if (std::holds_alternative<proto::Status>(*msg)) {
                const auto& ps = std::get<proto::Status>(*msg);
                ScreenFgStatus s;
                s.fps = ps.fps;
                s.mult = ps.mult;
                s.layer = ps.layer;
                s.state = ps.state;
                auto cb = statusCb_;
                if (cb)
                    postIdle([cb, s] { cb(s); });
            } else {
                int code = std::get<proto::Exit>(*msg).code;
                if (!exitReported_.exchange(true)) {
                    auto cb = exitCb_;
                    if (cb)
                        postIdle([cb, code] { cb(code); });
                }
            }
        }
    }
    if (eof) {
        // EOF：收屍 + 補 exit（若子程序被殺、沒發 exit JSON）
        int code = waitExitCode(pid_);
        if (!exitReported_.exchange(true)) {
            auto cb = exitCb_;
            if (cb)
                postIdle([cb, code] { cb(code); });
        }
    }
    running_.store(false);
}

void ScreenFgProcess::logLoop() {
    char chunk[8192];
    std::string pending;
    while (!stopping_.load()) {
        int pr = pollFd(stderrR_);
        if (pr < 0)
            break;
        if (pr == 0)
            continue;
        ssize_t r = read(stderrR_, chunk, sizeof chunk);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (r == 0)
            break; // EOF
        pending.append(chunk, (size_t) r);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            auto cb = logCb_;
            if (cb)
                postIdle([cb, line] { cb(line); });
        }
    }
}

void ScreenFgProcess::setStatusCb(std::function<void(const ScreenFgStatus&)> cb) {
    statusCb_ = std::move(cb);
}
void ScreenFgProcess::setExitCb(std::function<void(int)> cb) {
    exitCb_ = std::move(cb);
}
void ScreenFgProcess::setLogCb(std::function<void(const std::string&)> cb) {
    logCb_ = std::move(cb);
}

void ScreenFgProcess::sendCommand(const std::string& cmd) {
    std::lock_guard<std::mutex> lk(stdinMu_);
    if (stdinW_ < 0)
        return;
    std::string msg = cmd + "\n";
    writeAll(stdinW_, msg.data(), msg.size());
}

void ScreenFgProcess::quit() {
    sendCommand(proto::Cmd::Quit);
}
