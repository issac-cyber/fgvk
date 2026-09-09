# screen-fg GUI — Wayfinding Map

## Destination

一個**獨立桌面控制 App**（separate program）給 `screen-fg` 用：啟動 / 控制現有的 `screen-fg` binary，**與 CLI 並存**（CLI 保持完整可用，供驗證 / 腳本）。
GUI 本身不改 screen-fg 的 LOCKED v1.0 內部管線。

**四個功能（皆在 MVP）**：
1. **啟動 / 停止** — 一鍵啟動 / 結束，不用打指令
2. **狀態顯示** — FPS ／ 倍數 ／ layer 狀態
3. **內嵌選窗** — 取代系統 portal picker，自己選視窗
4. **執行中調設定** — 調 HUD ／ 暫停，不用重啟

## Notes

- 領域：screen-fg 的 GUI 控制層。CLI binary 本身是 `.scratch/screen-fg-pipeline/spec.md`（LOCKED v1.0）。
- 本機 = GNOME Wayland（gnome-shell / mutter）、Ubuntu 26.04、2× R9700、mesa radv。
- **關鍵硬點**：Wayland 安全模型通常不讓一個 client 列舉其他 client 的視窗 → 正是 portal picker 存在的原因。「內嵌選窗」的可行性是主要不確定項（ticket 01）。
- 每 session 應查的 skill：`domain-modeling`（維護術語表）、`grilling`（decision ticket）。
- screen-fg 目前**沒有**控制 / 狀態 interface（只有 stdout log + 視窗內 Esc/P 鍵）→ GUI 需要它暴露新 interface（ticket 03）。

## Decisions so far

<!-- 索引：每個 resolved ticket 一列 -->

- [01 Wayland 內嵌選窗可行性](issues/01-wayland-window-picker.md)：GNOME Wayland / mutter 上「真正的 in-app 選窗」**原生不可行**；可行選項＝保留 XDG portal picker（GUI 按鈕觸發既有流程）或（v2/可選）自製 GNOME Shell extension。見 `research/01-wayland-window-picker.md`。
- [02 GUI toolkit 可行性](issues/02-gui-toolkit-feasibility.md)：選用 **GTK4 + libadwaita**（C++ 走 gtkmm-4.0）；本機 runtime 全在、dev 頭一 `apt install` 即得、原生 GNOME Wayland 外觀 + widget 現成 + 依賴最小。見 `research/02-gui-toolkit.md`。
- [04 MVP 界線與驗收標準](issues/04-mvp-boundary-acceptance.md)：v1 = 獨立程式 **`screen-fg-gui`**（GTK4+libadwaita、binary+.desktop、不自啟）＋ 4 功能（啟動/停止、狀態顯示、選窗=**GUI 按鈕觸發既有 portal**、執行中調設定）；GUI 不改 LOCKED v1.0 管線、依賴 screen-fg 暴露控制/狀態 interface（→ 03）。驗收 = e2e 全鏈＋各功能驗收標準。
- [03 GUI ↔ screen-fg 控制通道（IPC）設計](issues/03-control-channel.md)：機制＝**stdio 父子管線**——GUI fork+exec screen-fg；命令走 stdin（`pause`/`resume`/`hud`/`quit`）、狀態走 stdout JSON line、log 走 stderr；選窗＝screen-fg 自己既有 portal picker（不改）；config 邊界＝啟動期 config.toml、執行中 pause/HUD；screen-fg 改動皆 additive（不碰 LOCKED v1.0 管線）。

## Not yet specified

- **錯誤 / 狀態呈現**：crash、無顯示、layer unload、來源視窗關閉（自動退出 Q9）在 GUI 上如何呈現（status JSON 帶 state/error；UI 細節）。
- **多目標 / 多執行個**：GUI 是否可同時驅動多個 screen-fg 執行個（疑似 v2）。

## Out of scope

- 改 screen-fg 捕捉 / 去重 / 呈現 / layer 管線內部（LOCKED v1.0 不動）。
- CLI 流程本身（保留，供驗證 / 腳本）。
- 插幀算法（lsfg-vk 是黑盒，不碰）。
- 非本機平台（只針對本機 GNOME Wayland）。
