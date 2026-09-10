#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace screenfg {

// 最終設定（persistent settings；env 已 resolve 進去）。
// 注意：只存 displayIndex（哪塊螢幕）；card 序号由純函數 cardIndexFor() 映射，
// 兩者不可混用。
struct Config {
    float dedupThreshold = 3.0f; // MAD on 0..255
    bool hud = true;
    std::string gpu; // PCI bus ID override (e.g. "0000:01:00.0"); empty = auto
    std::string profile = "2x FG / 100%"; // lsfg-vk profile name
    int displayIndex = 0; // which display the FG window covers
    uint32_t contentFpsCap = 60;
    std::string captureMode = "window"; // "window"（spec 預設：picker 選視窗）| "monitor"（全螢幕 monitor 捕捉、無 picker）
    int captureMonitor = 0; // 抓第幾條 stream（monitor 模式下 portal 回多條 streams；0 = 第一條）
};

// 環境變數的注入表示（可測性：不直接碰 getenv）。
using EnvMap = std::unordered_map<std::string, std::string>;

// resolve 的產物：persistent 設定 + 運行期 re-exec 狀態。
struct ResolvedConfig {
    Config config;
    bool paused = false; // 來自 SCREENFG_STATE == "paused"
};

// 純：flat-TOML 子集 text → Config（defaults 打底；malformed throw std::runtime_error）。
Config parseConfig(const std::string& tomlText, Config defaults);

// 純：config.toml text + env（HUD/DISPLAY/STATE）→ 最終設定。
// envMap 鍵（皆可缺）："SCREENFG_HUD"（覆蓋 hud）、"SCREENFG_DISPLAY"（覆蓋 displayIndex）、
//                       "SCREENFG_STATE"（== "paused" → paused）。
ResolvedConfig resolveConfig(const std::string& tomlText, const EnvMap& envMap);

// 純：display index → card 序号（本機 identity：display N 在 card N；
// 具名以區隔「display index」與「card 序号」兩個概念）。
int cardIndexFor(int displayIndex);

// 讀檔 adapter：缺檔案 → 純預設；malformed throw。env 由真實環境收集後交給純 resolveConfig。
ResolvedConfig loadConfig(const std::string& path);

} // namespace screenfg
