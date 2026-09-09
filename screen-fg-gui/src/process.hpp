#pragma once
// screen-fg 子程序控制（transport）：fork+exec、持 stdin/stdout 管線、寫命令、waitpid。
// 解碼（行 → status/exit）交給 shared/protocol.hpp 的純 ProtocolDecoder；本模組只負責 IO。
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ScreenFgStatus {
    int fps = 0;
    unsigned mult = 0;
    bool layer = false;
    std::string state; // "running" / "paused" / "exiting"
};

class ScreenFgProcess {
public:
    ScreenFgProcess();
    ~ScreenFgProcess();

    // 回傳是否成功 fork+exec
    bool start(const std::string& binary, const std::vector<std::string>& extraArgs);
    void sendCommand(const std::string& cmd); // 詞彙見 shared/protocol.hpp 的 Cmd
    void quit();                             // 送 quit 命令（退出由 exit callback 異步處理；destructor 會 bounded 等待 + SIGTERM 兜底）

    void setStatusCb(std::function<void(const ScreenFgStatus&)> cb);
    void setExitCb(std::function<void(int)> cb);
    void setLogCb(std::function<void(const std::string&)> cb); // 可選：子程序 stderr

    bool running() const { return running_.load(); }
    pid_t pid() const { return pid_; }

private:
    void readerLoop();
    void logLoop();

    pid_t pid_ = -1;
    int stdinW_ = -1; // 父持有：寫命令
    int stdoutR_ = -1; // 父持有：讀 status
    int stderrR_ = -1; // 父持有：讀 log（可選）
    std::thread reader_;
    std::thread stderrReader_;
    std::mutex stdinMu_;
    std::atomic<bool> running_{false};
    std::atomic<bool> exitReported_{false};
    std::atomic<bool> stopping_{false}; // destructor 設 true → reader 於 ~100ms 內退出（bounded join，不卡 app 退出）
    std::function<void(const ScreenFgStatus&)> statusCb_;
    std::function<void(int)> exitCb_;
    std::function<void(const std::string&)> logCb_;
};
