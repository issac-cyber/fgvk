# 03 GUI ↔ screen-fg 控制通道（IPC）設計

Type: grilling
Status: resolved

## Question

獨立 GUI 如何啟動 / 控制 / 讀狀態 screen-fg 程序？這決定 screen-fg 要暴露什麼新 interface。

子決策：
- GUI 如何啟動 screen-fg（subprocess + 哪些 args？传選窗結果怎麼给？）
- 執行中命令通道（暫停 / 恢復、HUD on/off…）— IPC 機制（DBUS？unix socket？signal？file？）
- 狀態通道（FPS / 倍數 / layer 狀態 / 錯誤狀態）— GUI 怎麼 poll / 訂閱
- 啟動期設定（改 config.toml + 重啟）vs 執行中設定（IPC 熱套）的邊界

注意：screen-fg 目前**沒有**控制 / 狀態 interface（只有 stdout log + 視窗內 Esc/P 鍵）。本 ticket 決定要新增什麼。
（HITL：與使用者對話決策；查 `grilling` + `domain-modeling`。）

## Answer

**控制通道（已定案）= stdio（父子管線）**

- **程序模型**：GUI（parent）`fork`+`exec` screen-fg（child），擁有 stdin/stdout 管線 + 生命週期。
- **選窗**：screen-fg 用**自己既有的 portal picker**（picker 完全不改）；GUI 按鈕只負責啟動。
- **命令通道（GUI→screen-fg）**：寫 line 到 screen-fg 的 **stdin** — `pause`、`resume`、`hud 0|1`、`quit`。screen-fg 在 event loop 讀 stdin（additive）。
- **狀態通道（screen-fg→GUI）**：發 **JSON line 到 stdout** — `{"type":"status","fps":N,"mult":N,"layer":bool,"state":"running|paused|exiting"}` + `{"type":"exit","code":N}`；**log 走 stderr**（additive）。
- **停止**：GUI 寫 `quit`（或 kill child）；screen-fg 自動退出（Q9）→ GUI 用 stdout EOF / `waitpid` 偵測。
- **config 邊界**：啟動期 = `~/.config/screen-fg/config.toml`（display、source）；執行中 = pause/HUD（走 stdin 命令）。
- **暫停/passthrough**：GUI 寫 `pause`/`resume`；screen-fg 觸發既有 re-exec-to-plain（PID 不變、管線保持連接）。
- **screen-fg 改動（皆 additive、不碰 LOCKED v1.0 管線）**：event loop 讀 stdin 命令、發 status JSON 到 stdout、log 改走 stderr、（可選）接受 config path arg。
- 精確 command 語法 / JSON schema 留到實作定；本 ticket 只定機制（stdio + stdin 命令 + stdout 狀態 + stderr log）。
