#pragma once
#include <optional>
#include <string>

namespace screenfg {

// re-exec 的 SCREENFG_STATE env 值（注意：與 status JSON 的 state 詞彙不同——
// status 用 "running"，re-exec env 用 "normal"，兩者不可混用）。
namespace ReexecEnv {
inline constexpr const char* Paused = "paused";
inline constexpr const char* Normal = "normal";
} // namespace ReexecEnv

// 請求的 re-exec 動作。
enum class ReexecAction {
    PassthroughBoot, // --passthrough：立即切到 plain 複製本
    Pause,           // 暫停：切到 plain 複製本（layer unload、純呈現）
    Resume,          // 恢復：切回 screen-fg
    Toggle,          // P 鍵：paused <-> normal
};

// 純 resolver 的產物：re-exec 到哪個 binary + 傳遞哪些 env。
struct ReexecPlan {
    std::string targetName; // "screen-fg" 或 "screen-fg-plain"
    std::string stateEnv;   // ReexecEnv::Paused / Normal
    std::string hudEnv;     // "1"/"0"；boot 為 ""（不設 SCREENFG_HUD）
};

// 純（無 I/O、無 self-path、不 execv）：给定當前狀態、請求動作、HUD，決定 re-exec 的
// target + 傳遞 env。self-path 由 adapter 注入（見 reexec.cpp）。
ReexecPlan resolveReexec(bool currentlyPaused, ReexecAction action, std::optional<bool> hud);

// ---------- 注入 self-path 的薄 adapter（碰 process / fs，不純）----------

// /proc/self/exe 的真實路徑（readlink；失敗回 ""）。
std::string selfPath();

// 確保同目錄有最新的 screen-fg-plain 複製本（cp -f，每次都重做避免過期）。
void ensurePlainCopy(const std::string& selfP);

// 依 plan re-exec（組 argv 跳過 --passthrough、setenv STATE、有值才 setenv HUD、execv）。
// 成功不返回；失敗 exit(1)。
void reexec(const ReexecPlan& plan, int argc, char** argv, const std::string& selfP);

} // namespace screenfg
