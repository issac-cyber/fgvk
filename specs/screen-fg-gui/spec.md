# screen-fg-gui — 實作 Spec（Handoff）

> 由 wayfinder map `.scratch/screen-fg-gui/map.md`（4 個 resolved ticket：01 選窗 / 02 toolkit / 03 控制通道 / 04 MVP 界線）整合而成。規劃已鎖，依此實作。

## 1. 概觀

**`screen-fg-gui`** = 一個**獨立桌面控制 App**（GNOME Wayland），啟動 / 控制現有的 `screen-fg` binary，**與 CLI 並存**（CLI 完整保留，供驗證 / 腳本）。
GUI **不改** screen-fg 的 LOCKED v1.0 內部管線（捕捉 / 去重 / 呈現 / layer）；對 screen-fg 的改動**全部 additive**（見 §5）。

## 2. 架構

```
screen-fg-gui (parent, GTK4+libadwaita)
  │  fork + exec
  ▼
screen-fg (child, 既有 CLI binary)
  ├─ 命令通道：GUI 寫 line → screen-fg stdin
  ├─ 狀態通道：screen-fg 發 JSON line → stdout
  └─ log：screen-fg → stderr
```

- **程序模型**：GUI = parent，`fork`+`exec` screen-fg（child），擁有 stdin/stdout 管線 + 生命週期（`waitpid`）。
- **選窗**：screen-fg 用**自己既有的 XDG portal picker**（完全不改）。GUI 的「啟動」鈕只負責 fork+exec；picker 由 screen-fg 彈出。（in-app 原生選窗依 ticket 01 在 mutter 不可行；v2 可選 GNOME extension。）
- **toolkit**：GTK4 + libadwaita，C++ 走 **gtkmm-4.0**（ticket 02；本機 runtime 全在，dev 頭 `apt install libgtkmm-4.0-dev libadwaita-dev`）。

## 3. 四個功能（MVP，ticket 04）

| 功能 | 行為 |
|------|------|
| 啟動/停止 | 按鈕 fork+exec screen-fg（→ 它彈 portal picker、選窗、啟動捕捉、全螢幕呈現）；停止鈕寫 `quit`（或 kill child）。GUI 顯示 running 狀態。 |
| 狀態顯示 | 即時顯示 FPS ／ 倍數 ／ layer 狀態（讀 stdout status JSON）。 |
| 選窗 | = 啟動流程的一部分（screen-fg 自己的 portal picker）。 |
| 執行中調設定 | HUD on/off、暫停 / 恢復（寫 stdin 命令，立即生效、不重啟）。 |

**安裝**：`screen-fg-gui` binary 進 PATH + 一個 `.desktop` 圖示（可點選啟動、不自啟）。

## 4. 控制協議（ticket 03，baseline — 精確語法實作可微調）

- **命令（GUI → screen-fg stdin，line-based）**：
  - `pause` — 暫停（觸發 screen-fg 既有 re-exec-to-plain；PID 不變、管線保持）
  - `resume` — 恢復
  - `hud 0` / `hud 1` — HUD 關 / 開
  - `quit` — 乾淨退出
- **狀態（screen-fg → stdout，JSON line）**：
  - `{"type":"status","fps":N,"mult":N,"layer":true|false,"state":"running|paused|exiting"}`
  - `{"type":"exit","code":N}`
- **log**：screen-fg → **stderr**（目前走 stdout，需改）。
- **停止偵測**：GUI 用 stdout EOF / `waitpid` 偵測 child 結束。
- **config 邊界**：啟動期 = `~/.config/screen-fg/config.toml`（display、source 等）；執行中 = pause / HUD（走 stdin）。

## 5. screen-fg 的 additive 改動清單（不碰 LOCKED v1.0 管線）

1. **event loop 讀 stdin**：單執行緒 poll loop 加入 stdin fd；解析命令（`pause`/`resume`/`hud`/`quit`）並觸發對應動作。
2. **發 status JSON 到 stdout**：每（約）秒或每個新幀發一列 status JSON（fps / mult / layer / state）。
3. **發 exit JSON**：正常 / 異常結束時發 `{"type":"exit","code":N}`。
4. **log 改走 stderr**：現有 stdout log 全部改 stderr（stdout 保留給 status JSON）。
5. （可選）**接受 config path arg**：`screen-fg --config <path>`（預設仍 `~/.config/screen-fg/config.toml`）。

> 驗證：改動後 `SCREENFG_SYNTHETIC=1` 無門戶 synthetic（有 / 無 layer）仍须 exit 0；37/37 單測仍綠。

## 6. screen-fg-gui 程式結構（新）

- **main window（libadwaita）**：
  - 啟動 / 停止 鈕
  - 狀態區：FPS ／ 倍數 ／ layer 狀態（即時）
  - 控制：暫停 / 恢復、HUD on/off
  - running 狀態指示
- **process control**：`fork`+`exec` screen-fg；持有 stdin/stdout `Glib::UnixInputStream/OutputStream`（或 poll loop）；解析 stdout JSON 更新狀態；寫 stdin 命令；`waitpid` 偵測結束。
- **build**：CMake（`pkg-config gtkmm-4.0 libadwaita-1`），產 `screen-fg-gui` binary + `.desktop`。

## 7. 驗收標準（ticket 04）

- **e2e**：GUI 啟動 → portal 選窗 → 捕捉 → 全螢幕呈現 → lsfg-vk 插幀 → 乾淨停止（exit 0），全通。
- **啟動/停止**：啟動鈕 → 程序起來 + 呈現出現；停止鈕 → 乾淨退出；狀態顯示正確。
- **狀態**：FPS ／ 倍數 ／ layer 狀態在 GUI 即時更新。
- **選窗**：啟動 → portal 彈出 → 選窗 → 捕捉啟動。
- **調設定**：HUD ／ 暫停立即生效、不重啟。
- **安裝**：`screen-fg-gui` 進 PATH + `.desktop` 圖示，可點選啟動。
- **regression**：screen-fg 的 synthetic（有 / 無 layer）+ 37/37 單測仍綠。

## 8. Out of scope

- 改 screen-fg 捕捉 / 去重 / 呈現 / layer 管線內部（LOCKED v1.0 不動）。
- CLI 流程本身（保留）。
- 插幀算法（lsfg-vk 黑盒）。
- 非本機平台（只針對本機 GNOME Wayland）。

## 9. Fog / v2（非阻擋）

- **錯誤 / 狀態呈現 UI 細節**：crash、無顯示、layer unload、來源視窗關閉（Q9）在 GUI 上如何呈現（status JSON 已帶 state / error）。
- **多目標 / 多執行個**：GUI 同時驅動多個 screen-fg 執行個。
- **in-app 原生選窗**：自製 GNOME Shell extension（ticket 01 的 v2 選項）。
