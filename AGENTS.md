# fgvk

fg（frame generation）專案：**影片的即時插幀**（本機個人用）。機器：Ubuntu 26.04 / GNOME Wayland / 2× AMD R9700（各帶一屏 3440×1440）/ lsfg-vk 2.0.0 已裝（shader container 已恢復）。音頻：各路線不同（screen-fg = 來源照舊播＋工具靜音；extension = 來源 tab 靜音＋mpv 播音，見各自 AGENTS.md）。

## 現況（FG 路線結論）

- **lsfg-vk FG 引擎 = 活的**（`lsfg-vk-cli benchmark` 2× 正常）。
- **Chromium 瀏覽器內 FG = 死**：瀏覽器合成器以顯示 refresh（165/174Hz）present → FG 被 cap → 無效（`--enable-unsafe-swiftshader` 實測確認）。
- **screen-fg portal 捕捉 = 壞**：source 出一幀後 freeze（PipeWire buffer pool 死鎖，xdpw#395）；`SPA_PARAM_Buffers` 路徑在 libspa-videoconvert 段缺。
- **可用的 FG 路線 = mpv 外部視窗 + lsfg-vk**：`mpv --vo=gpu --gpu-api=vulkan`（vsync pacing swapchain）→ layer 攔截 → 插幀生效。`youtube-fg-extension/` 組件走這條。
- Wayfinder map：`.scratch/fgvk-route-pivot/map.md`（Final conclusion）。

## Scope

四個同層組件＋ spec＋ 設計文件。**相對結構不可動**：
- `shared/` 是兩專案的 sibling；兩份 CMakeLists 都以 `-I <fgvk>/` 引用它（`#include "shared/protocol.hpp"`）
- `screen-fg-gui` 靠 `$SCREENFG_BIN` → `../screen-fg/build/screen-fg`（完整順序見 `screen-fg-gui/AGENTS.md`）找 binary

| 組件 | 內容 |
|------|------|
| `screen-fg/` | 主 binary：portal ScreenCast v5 捕捉 → CPU 去重（32×32 MAD）→ SDL3 全螢幕 + Vulkan FIFO swapchain 呈現 → `lsfg-vk` implicit layer 插幀；另提供 stdio 控制通道（stdin 命令 / stdout status JSON / stderr log）。依 `specs/screen-fg-pipeline/spec.md`（LOCKED v1.0，2026-09-09；§13 為建置偏離附錄） |
| `screen-fg-gui/` | GUI 控制器（gtkmm-4.0，獨立程式，fork+exec `screen-fg` 走 stdio 控制）。依 `specs/screen-fg-gui/spec.md` |
| `shared/` | 純函式協議模組（`protocol.hpp`，兩專案共用的 stdio 控制協議**單一來源**） |
| `specs/` | 兩份 spec + 設計史。`screen-fg-pipeline/`：`spec.md` + `map.md` + `issues/`（研究筆記 01–03、spec lock 記錄 04、shader container 05；**無獨立 `research/`**）；`screen-fg-gui/`：`spec.md` + `map.md` + `issues/`（01–04）+ `research/`（01–02） |
| `docs/` | 設計/建置計畫記錄（dated record，例 `superpowers/plans/2026-09-09-build-fgvk-from-zero.md`；不改寫歷史） |
| `explainer.html` | 專案解說頁（standalone HTML，與 AGENTS.md 平行的溝通文件） |
| `youtube-fg-extension/` | **可用的 FG 路線**（Chrome extension + native messaging host）：popup 2x/3x/4x/10x → host `fgvk-mpv-launch.py` → `mpv --vo=gpu --gpu-api=vulkan` + `LSFGVK_PROFILE` + `MESA_VK_DEVICE_SELECT`（GPU1）。見 `youtube-fg-extension/AGENTS.md` |

各組件的建置 / 操作 / 陷阱全在**它自己的 `AGENTS.md`**（本檔只索引＋跨組件合約，不重複）。

## 建置 & 驗證

| 做什麼 | 命令 |
|--------|------|
| 建置 screen-fg | `cd screen-fg && cmake -B build && cmake --build build` |
| screen-fg 純模組測試（74 checks） | `cd screen-fg && ./build/screen-fg-tests` |
| 建置 screen-fg-gui | `cd screen-fg-gui && cmake -B build && cmake --build build` |
| GUI ProtocolDecoder 測試 | `cd screen-fg-gui && ./build/screen-fg-gui-tests` |
| 無門戶 e2e（synthetic） | `cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 ./build/screen-fg` |
| extension host selftest | `youtube-fg-extension/host/fgvk-mpv-launch.py --selftest` |
| extension 單元測試（node） | `cd youtube-fg-extension && node test/url-utils.test.cjs` |
| extension e2e（手動驗證） | 完全重啟 Chrome → youtube.com → 點擴充圖示 → 2x → 看 mpv 視窗；popup「Last launch」顯示成功/失敗 |

- synthetic 正常輸出：stderr 出現 `synthetic 模式` → `結束（3 帧捕捉 / 1 帧呈現）`，exit 0（合成幀的 16×16 移動方塊太小、動不了 32×32 MAD 超過預設閾值 3.0 → dedup 只放行第一幀；「1 帧呈現」是預期值）
- 加 `DISABLE_LSFGVK=1` = 關 layer、只驗呈現管線；不設 = 連 lsfg-vk layer 一起驗
- 系統依賴：`libsdl3-dev libpipewire-0.3-dev libvulkan-dev libglib2.0-dev`（screen-fg 的 portal 走 GDBus/gio-2.0、非 raw libdbus）；GUI：`libgtkmm-4.0-dev`（不需 libadwaita——無 C++ binding）
- layer 前置：Steam Lossless Scaling 保持在 `lsfg-vk` update channel（spec §6）

## 跨組件合約（改任何一項要同步三處：protocol.hpp + screen-fg + screen-fg-gui）

- 協議單一來源 `shared/protocol.hpp`：header-only 純函式、無 I/O、無外部 C 庫；兩專案編同一份 header，詞彙/結構不可能漂移；`kVersion=1`
- 命令（GUI → screen-fg stdin，行式）：`pause` / `resume` / `hud 0|1` / `quit`
- status（screen-fg → stdout，JSON 行）：`{"type":"status","fps":N,"mult":N,"layer":bool,"state":"running|paused|exiting"}` + `{"type":"exit","code":N}`
- screen-fg 的 stdout **只有**協議 JSON 行；log 全走 stderr（GUI 收進 log 區）
- `parse` 缺欄/格式錯 → `nullopt`（明確跳過、**不默默 default**）
- 暫停/恢復 = **re-exec**（screen-fg 端；layer 無法運行期切換）——GUI 只發命令、不重啟程序

## 配置 & 運行期檔案

| 檔案 | 用途 |
|------|------|
| `~/.config/screen-fg/config.toml` | screen-fg 持久設定（flat-TOML 子集；缺檔案 = 純預設；key 見 `screen-fg/AGENTS.md`） |
| `~/.config/lsfg-vk/conf.toml` | lsfg-vk layer 設定；現 4 支 FG profile（2x/3x/4x/10x / 100%，multiplier 2/3/4/10，vsync + override_present_mode + performance_mode）；extension 用 `LSFGVK_PROFILE` 選 profile（不需 active_in）。**⚠️ lsfg-vk GUI（Lossless Scaling app）存檔時會覆蓋此檔**——GUI 裡存過檔會把這 4 支 profile 清掉，要在 GUI 內重新加（或別在 GUI 存檔）；備份 `conf.toml.bak-btn4`（本次修前）、`conf.toml.bak-ext`、`conf.toml.bak-screenfg` |
| `~/.local/share/vulkan/implicit_layer.d/` | lsfg-vk implicit layer（`VK_LAYER_LSFGVK_frame_generation`）位置；未 enumerate 到時 screen-fg 啟動警告 |
| `~/.local/share/applications/screen-fg-gui.desktop` | GUI 桌面圖示（安裝方式見 `screen-fg-gui/AGENTS.md`） |

## 環境變數速查

| 變數 | 使用方 | 用途 |
|------|--------|------|
| `SCREENFG_HUD` | screen-fg | `"1"`/`"0"` 覆蓋 config `hud`；re-exec 跨进程保留運行期 HUD 狀態 |
| `SCREENFG_DISPLAY` | screen-fg | 覆蓋 config `display`（FG 視窗蓋的螢幕 index；GPU 跟該 display 走） |
| `SCREENFG_STATE` | screen-fg | `"paused"` → 啟動即 passthrough（re-exec 帶 env）；注意：re-exec env 詞彙是 `"normal"`/`"paused"`，與 status JSON 的 `"running"`/`"paused"` 是**不同詞彙**，不可混用 |
| `SCREENFG_SYNTHETIC` | screen-fg | 非 `"0"` = 合成幀（不需 portal/picker，驗證呈現管線 + layer） |
| `SCREENFG_SYNTHETIC_FRAMES` | screen-fg | 合成幀數（預設 300） |
| `DISABLE_LSFGVK` | lsfg-vk layer | `"1"` = 關掉 layer（只驗呈現管線） |
| `DBUS_SESSION_BUS_ADDRESS` | screen-fg | 必須是乾淨 bus 路徑（env 帶過期 guid 會 "Did not receive a reply"；詳 `screen-fg/AGENTS.md`） |
| `SCREENFG_BIN` | screen-fg-gui | screen-fg binary 路徑（search 順序第一優先） |
| `LSFGVK_PROFILE` | lsfg-vk layer | 用名稱選 profile（extension host 用它設 2x/3x/4x/10x；不需 active_in） |
| `MESA_VK_DEVICE_SELECT` | Mesa device-select layer | 固定特定 GPU（extension host 用 `1002:7551:0000:07:00.0` = GPU1／第二張 R9700；兩張 R9700 同 ID 1002:7551，須用 PCI BDF 區分） |

## 命名

`fgvk` 是專案/目錄名；binary 仍叫 `screen-fg`（`screen-fg-plain` 複製本、`~/.config/screen-fg/`、layer `active_in`="screen-fg" 全維持）。搜尋 code 搜 `screen-fg`，不要搜 `fgvk`。
