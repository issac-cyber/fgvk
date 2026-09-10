#include "capture.hpp"
#include "clamp.hpp"
#include "config.hpp"
#include "dedup.hpp"
#include "frame_source.hpp"
#include "present.hpp"
#include "reexec.hpp"
#include "synthetic.hpp"

#include "shared/protocol.hpp"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <sys/stat.h>

#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace screenfg;

namespace {

// ---------- health check ----------
bool layerEnumerated() {
    uint32_t n = 0;
    if (vkEnumerateInstanceLayerProperties(&n, nullptr) != VK_SUCCESS || n == 0)
        return false;
    std::vector<VkLayerProperties> props(n);
    if (vkEnumerateInstanceLayerProperties(&n, props.data()) != VK_SUCCESS)
        return false;
    for (auto& p : props)
        if (strcmp(p.layerName, "VK_LAYER_LSFGVK_frame_generation") == 0)
            return true;
    return false;
}

// conf.toml 裡有沒有 active_in 包含 "screen-fg" 的 profile（FG 才會啟動）
bool confHasActiveScreenFg() {
    const char* home = getenv("HOME");
    if (!home)
        return false;
    std::ifstream f(std::string(home) + "/.config/lsfg-vk/conf.toml");
    if (!f)
        return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("active_in") != std::string::npos &&
            line.find("\"screen-fg\"") != std::string::npos)
            return true;
    }
    return false;
}

uint64_t readNumFile(const std::string& path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return strtoull(s.c_str(), nullptr, 10);
}

// ---------- HUD ----------
char hudBuf[96];
void makeHud(char* buf, size_t n, int fps, uint32_t mult, const char* profile, uint32_t hz, bool paused) {
    if (paused)
        snprintf(buf, n, "PAUSED  FPS %d  %dHz", fps, hz);
    else
        snprintf(buf, n, "FPS %d  X%d  %s  %dHz", fps, mult, profile, hz);
}

// ---------- GUI stdio channel ----------
// stdout = status JSON（只有 JSON 行，log 全走 stderr）；stdin = 控制命令（行式）
void writeAll(int fd, const char* b, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = write(fd, b + off, n - off);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break; // EPIPE 等：GUI 已關閉
        }
        off += (size_t) r;
    }
}

// 發端：protocol::emit* 產整行 bytes（純），本 transport 負責寫（含 EPIPE/EINTR 處理）。
void emitStatus(int fps, uint32_t mult, bool layer, const char* state) {
    std::string s = protocol::emitStatus(fps, mult, layer, state);
    writeAll(STDOUT_FILENO, s.data(), s.size());
}

void emitExit(int code) {
    std::string s = protocol::emitExit(code);
    writeAll(STDOUT_FILENO, s.data(), s.size());
}

struct CtrlCmd { std::string name; std::string arg; };

// 非阻擋：poll stdin（fd 0），累積完整行，回傳一個命令（無則 nullopt）
std::optional<CtrlCmd> pollStdinCmd() {
    static std::string pending;
    struct pollfd p;
    p.fd = 0;
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, 0) > 0) {
        char buf[256];
        ssize_t r = read(0, buf, sizeof buf);
        if (r > 0)
            pending.append(buf, (size_t) r);
    }
    size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
        std::string cmd = pending.substr(0, nl);
        pending.erase(0, nl + 1);
        if (!cmd.empty() && cmd.back() == '\r')
            cmd.pop_back();
        if (cmd.empty())
            continue;
        CtrlCmd out;
        size_t sp = cmd.find(' ');
        if (sp == std::string::npos)
            out.name = cmd;
        else {
            out.name = cmd.substr(0, sp);
            out.arg = cmd.substr(sp + 1);
        }
        for (auto& c : out.name)
            c = (char) tolower((unsigned char) c);
        return out;
    }
    return std::nullopt;
}

} // namespace

// pw_init 必須在 pw_main_loop_new 之前呼叫（實測：缺了它 pw_main_loop_new 載不動
// support.system handle → "No such file or directory"；pw-dump 等工具就是靠它）。
// RAII guard 保證每個 return 路徑都跑 pw_deinit（含 error return）。
struct PwInitGuard {
    explicit PwInitGuard(int* a, char*** v) { pw_init(a, v); }
    ~PwInitGuard() { pw_deinit(); }
};

int main(int argc, char** argv) {
    std::string selfP = selfPath();
    ensurePlainCopy(selfP);
    PwInitGuard pwGuard(&argc, &argv);

    // 1. config（--config <path> 可選；否則用預設路徑）；
    //    resolveConfig 把 toml + env（HUD/DISPLAY/STATE）一次性 resolve 成最終設定 + paused
    std::string cfgPath;
    for (int i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "--config") == 0) {
            cfgPath = argv[i + 1];
            break;
        }
    if (cfgPath.empty()) {
        const char* home = getenv("HOME");
        cfgPath = std::string(home ? home : "") + "/.config/screen-fg/config.toml";
    }
    ResolvedConfig rc = loadConfig(cfgPath);
    Config& cfg = rc.config;
    bool hudOn = cfg.hud; // env（SCREENFG_HUD）已在 resolveConfig resolve；跨 re-exec 保留運行期 HUD
    bool paused = rc.paused; // 來自 SCREENFG_STATE == "paused"

    // --passthrough：立即 re-exec 成 plain 複製本（無 FG 的純呈現）；boot 不設 HUD env
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--passthrough") == 0)
            reexec(resolveReexec(false, ReexecAction::PassthroughBoot, std::nullopt), argc, argv, selfP);
    }
    if (paused)
        std::cerr << "[screen-fg] passthrough 模式（layer 已 unload，無插幀）\n";

    // 2. frame 來源：預設走 portal 捕捉（先跑 picker，別被全螢幕視窗蓋住）；
    //    SCREENFG_SYNTHETIC=1 → 合成幀（不需 portal/picker，用於驗證呈現管線 + layer）
    const char* synEnv = getenv("SCREENFG_SYNTHETIC");
    const bool synthetic = synEnv && synEnv[0] != '0' && synEnv[0] != '\0';
    uint32_t synFrames = 300;
    if (synthetic) {
        const char* n = getenv("SCREENFG_SYNTHETIC_FRAMES");
        if (n && n[0])
            synFrames = static_cast<uint32_t>(strtoul(n, nullptr, 10));
        std::cerr << "[screen-fg] synthetic 模式（合成 " << synFrames
                  << " 帧，驗證呈現管線 + layer）\n";
    }
    std::unique_ptr<FrameSource> source;
    if (synthetic)
        source = std::make_unique<SyntheticSource>(1280, 720, synFrames);
    else {
        auto cap = std::make_unique<Capture>();
        cap->setMonitorMode(cfg.captureMode == "monitor");
        source = std::move(cap);
    }
    try {
        source->start();
    } catch (const std::exception& e) {
        std::cerr << "[screen-fg] 來源啟動失敗：" << e.what() << "\n";
        emitExit(1);
        return 1;
    }

    // 3. 呈現（視窗 + swapchain + GPU 選擇）
    Presenter presenter;
    PresentParams pp;
    pp.hud = hudOn;
    pp.displayIndex = cfg.displayIndex; // env（SCREENFG_DISPLAY）已在 resolveConfig resolve
    pp.gpuPciId = cfg.gpu.empty() ? nullptr : cfg.gpu.c_str();
    try {
        presenter.init(pp);
    } catch (const std::exception& e) {
        std::cerr << "[screen-fg] 呈現初始化失敗：" << e.what() << "\n";
        source->stop();
        emitExit(1);
        return 1;
    }
    uint32_t hz = presenter.displayHz();

    // 4. clamp（Q4）+ health check
    uint32_t maxMult = maxMultiplierForCapture(hz);
    if (maxMult < 1)
        maxMult = 1;
    if (cfg.contentFpsCap < 60) {
        uint32_t m = maxMultiplier(hz, cfg.contentFpsCap);
        if (m < maxMult) {
            maxMult = m;
            std::cerr << "[screen-fg] 警告：倍數 clamp 到 " << maxMult << "x（" << hz << "Hz / "
                     << cfg.contentFpsCap << "fps cap）\n";
        }
    } else {
        std::cerr << "[screen-fg] 最大倍數 " << maxMult << "x（" << hz << "Hz / 60fps 捕捉）\n";
    }
    if (!layerEnumerated())
        std::cerr << "[screen-fg] 警告：找不到 layer VK_LAYER_LSFGVK_frame_generation"
                     "（檢查 ~/.local/share/vulkan/implicit_layer.d/）\n";
    if (!confHasActiveScreenFg())
        std::cerr << "[screen-fg] 警告：conf.toml 沒有 active_in 含 \"screen-fg\" 的 profile"
                     " → layer 會自我 unload、FG 不會啟動\n";
    {
        char path[256];
        snprintf(path, sizeof path, "/sys/class/drm/card%d/device", cardIndexFor(cfg.displayIndex));
        uint64_t total = readNumFile(std::string(path) + "/mem_info_vram_total");
        uint64_t used = readNumFile(std::string(path) + "/mem_info_vram_used");
        if (total > 0 && used < total) {
            uint64_t freeMiB = (total - used) / (1024 * 1024);
            if (freeMiB < 2048)
                std::cerr << "[screen-fg] 警告：VRAM 只剩 " << freeMiB << " MB（可能有其他程序在佔 GPU）\n";
        }
    }

    // 5. 主迴圈
    Deduplicator dedup(cfg.dedupThreshold);
    int64_t frameCount = 0;
    int64_t presentCount = 0;
    int fps = 0;
    auto fpsT0 = std::chrono::steady_clock::now();
    int fpsN = 0;
    bool running = true;
    auto lastStatusAt = std::chrono::steady_clock::now();

    // 初始 status（啟動即發，讓 GUI 馬上拿到 state）
    emitStatus(0, maxMult, !paused, paused ? protocol::State::Paused : protocol::State::Running);

    while (running) {
        // 狀態心跳（1Hz，與是否收到幀無關，讓 GUI 穩定更新）
        auto nowS = std::chrono::steady_clock::now();
        if (nowS - lastStatusAt >= std::chrono::seconds(1)) {
            lastStatusAt = nowS;
            emitStatus(fps, maxMult, !paused, paused ? protocol::State::Paused : protocol::State::Running);
        }

        // stdin 控制命令（GUI stdio channel）
        if (auto cmd = pollStdinCmd()) {
            if (cmd->name == protocol::Cmd::Quit) {
                running = false;
            } else if (cmd->name == protocol::Cmd::Pause) {
                if (!paused)
                    reexec(resolveReexec(paused, ReexecAction::Pause, hudOn), argc, argv, selfP);
            } else if (cmd->name == protocol::Cmd::Resume) {
                if (paused)
                    reexec(resolveReexec(paused, ReexecAction::Resume, hudOn), argc, argv, selfP);
            } else if (cmd->name == protocol::Cmd::Hud) {
                hudOn = (cmd->arg == "1");
                presenter.setHud(hudOn);
            }
        }
        if (!running)
            break;

        SDL_Event ev;
        while (SDL_WaitEventTimeout(&ev, 200) > 0) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_ESCAPE) {
                    running = false;
                    break;
                }
                if (ev.key.key == SDLK_P) {
                    // Q8a：暫停 = re-exec 到 plain 複製本（繼續呈現原始幀、無插幀）
                    reexec(resolveReexec(paused, ReexecAction::Toggle, hudOn), argc, argv, selfP);
                }
            }
        }
        if (!running)
            break;

        source->poll();
        if (source->isDead()) {
            std::cerr << "[screen-fg] 來源結束（synthetic 跑完 / 來源視窗關閉）→ 退出（Q9）\n";
            break;
        }

        auto frame = source->nextFrame();
        if (!frame)
            continue;

        // fps
        fpsN++;
        auto now = std::chrono::steady_clock::now();
        if (now - fpsT0 >= std::chrono::seconds(1)) {
            fps = fpsN;
            fpsN = 0;
            fpsT0 = now;
        }
        frameCount++;

        // dedup（Q2）：frame 已是 CPU 端的 BGRA copy，直接比對
        bool present = dedup.isDifferent(frame->data, frame->width, frame->height);

        if (present) {
            presentCount++;
            makeHud(hudBuf, sizeof hudBuf, fps, maxMult,
                   cfg.profile.c_str(), hz, paused);
            if (hudOn)
                presenter.setHudText(hudBuf);
            presenter.presentFrame(*frame);
        }
    }

    emitStatus(fps, maxMult, !paused, protocol::State::Exiting);
    std::cerr << "[screen-fg] 結束（" << frameCount << " 帧捕捉 / " << presentCount
               << " 帧呈現）\n";
    source->stop();
    presenter.shutdown();
    emitExit(0);
    return 0;
}
