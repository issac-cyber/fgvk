# screen-fg-gui

`screen-fg` 的 Linux 桌面 GUI 控制器（獨立程式，與 CLI 並存）。依 `../specs/screen-fg-gui/spec.md` 建置（4 個 resolved ticket：選窗 / toolkit / 控制通道 / MVP 界線）。

## Scope

GTK4（C++ 走 gtkmm-4.0；本機 libadwaita 無 C++ binding 故未連結，GNOME 預設 GTK4 主題即 Adwaita 故外觀相同）單一 binary `screen-fg-gui`：`fork`+`exec` 啟動 `screen-fg` 子程序、stdio 控制（命令走子程序 stdin、status JSON 走 stdout）、主窗戶提供 啟動/停止、狀態顯示（FPS/倍數/layer/狀態）、選窗（GUI 按鈕觸發 screen-fg 自己的 XDG portal picker）、執行中設定（HUD on/off、暫停/恢復）。不改 `screen-fg` 的 LOCKED v1.0 管線。App ID `com.issac.screenfg-gui`；主窗 560×640。

## Key files

- `CMakeLists.txt` — 建置（需 `libgtkmm-4.0-dev`；**不**需 libadwaita，無 C++ binding；`-I <fgvk>/` 引用 `shared/`；含純 `screen-fg-gui-tests` target，ctest 目標名 `unit`）
- `src/main.cpp` — `GuiApp`（`Gtk::Application` subclass；`on_activate` 建 `MainWindow` + `add_window`）＋`MainWindow`（純 gtkmm `Gtk::Window`）＋binary 自動偵測；layout：binary 路徑 Entry、啟動/停止鈕、狀態列（FPS/mult/layer/state）、暫停/恢復鈕（tooltip 警告重新選窗）、HUD switch、log TextView（ScrolledWindow 包住）；命令/狀態詞彙走 `shared/protocol.hpp`；status/exit/log 回調全經 `postIdle` 排回 GLib 主執行緒
- `src/process.{hpp,cpp}` — 子程序控制 **transport**（fork+exec、stdin/stdout/stderr 三條管線、寫命令、waitpid、reader/log 兩線程、destructor bounded 清理）；**解碼**（行 → status/exit）交給 `shared/protocol.hpp` 的純 `parse`；本模組只負責 IO + 回調
- `shared/protocol.hpp`（兄弟目錄 `../shared/`）— stdio 控制協議**單一來源**（與 `screen-fg` 共用）：`Message`、詞彙常數、`kVersion`、純 emit/parse + 欄位存在性檢查
- `tests/test_protocol_decoder.cpp` — 純 ProtocolDecoder 測試（不 link gtkmm）
- `screen-fg-gui.desktop` — 桌面圖示（`Name[zh_TW]=screen-fg 插帧控制`、`Exec=screen-fg-gui`、`Icon=video-display`、`Categories=Video;`）

## 操作

- 建置：`cmake -B build && cmake --build build`
- 跑純 ProtocolDecoder 測試：`./build/screen-fg-gui-tests`（不 link gtkmm；依賴 `../shared/`）
- 執行：`./build/screen-fg-gui`（自動找 `screen-fg` binary，順序見下；全找不到 → Entry 顯示「（找不到，請手動填路徑）」、啟動鈕被擋）
- 桌面圖示：把 `screen-fg-gui` 裝到 PATH 後，`cp screen-fg-gui.desktop ~/.local/share/applications/`

### binary 自動偵測（`findBinary` 順序）

1. `$SCREENFG_BIN`（env，路徑存在即採用）
2. `build/screen-fg`（CWD 相對）
3. `../screen-fg/build/screen-fg`
4. `/usr/local/bin/screen-fg`
5. `<screen-fg-gui 執行檔目錄>/../screen-fg/build/screen-fg`
6. 掃描 `PATH` 每一段（`<dir>/screen-fg`）

## 執行期行為（main.cpp）

- 啟動/停止鈕狀態機：未 running → `proc_.start(bin, {})`（鈕變「停止」、state 顯示 "starting…"）；running → `proc_.quit()`（送 quit 命令、退出由 exit 回調**異步**處理）
- state 標籤：status 的 `state`（running/paused/exiting）→ 退出時 code 0 = 「已停止」、非 0 = 「錯誤結束 (N)」
- 暫停/恢復鈕：標籤跟 state（running→「暫停」、paused→「恢復」）；按下發 pause/resume 命令；log + tooltip 警告：**暫停會重新啟動捕捉（真實捕捉模式下會重跑選窗）**
- HUD switch → `hud 1|0` 命令（立即生效、不重啟）
- log 區：子程序 stderr 逐行（logLoop）+ GUI 端事件（啟動/停止/退出/暫停警告）
- 狀態列由 `onStatus` 更新（FPS / 倍數 / layer on-off / state + 暫停鈕標籤）

## 子程序控制細節（process.cpp）

- `start()`：三條 pipe（stdin/stdout/stderr）+ `fork`；子 `dup2` + `execv`（失敗 `_exit(127)`）；父留 `stdinW_`/`stdoutR_`/`stderrR_` + 起 `readerLoop` + `logLoop` 兩線程；thread 建立失敗 → `SIGKILL` 子程序 + 清 fd（不 leak）
- `readerLoop`：`poll`（100ms 超時）+ 8KB chunked `read` + `pending` buffer 抽行（去尾 `\r`）→ 純 `proto::parse`（nullopt 跳過）→ Status → `postIdle` 回 GLib 主執行緒回調；**EOF** → `waitExitCode`（WIFEXITED → exit status；WIFSIGNALED → 128+sig）＋補 exit 回調（子被殺、沒發 exit JSON 時）
- `logLoop`：同樣模式，stderr → `logCb`
- `sendCommand`：mutex 保護、寫 `cmd + "\n"`（EINTR continue / EPIPE 放棄）
- destructor：若 running 先送 quit → `stopping_=true`（reader ~100ms 內退出、bounded join、不卡 app 退出）→ 關 stdin（子程序 EOF）→ join 兩線程 → `waitpid(WNOHANG)`；子程序還活就 `kill(SIGTERM)` 確保收掉（否則 orphan 給 init）

## 控制協議（與 screen-fg 的 stdio 通道）

**協議的單一來源是 `shared/protocol.hpp`**（兩端編同一份 header，詞彙/結構不可能漂移）：
- 命令（寫子程序 stdin，行式）：`pause` / `resume` / `hud 0|1` / `quit`（`protocol::Cmd`）
- status JSON（讀子程序 stdout）：`{"type":"status","fps":N,"mult":N,"layer":bool,"state":"running|paused|exiting"}`（`protocol::State`）
- exit JSON：`{"type":"exit","code":N}`
- 子程序 log 走 stderr（GUI 收進 log 區）
- **解碼是純函數** `protocol::parse(line) → optional<Message>`：缺欄/格式錯 → `nullopt`（明確跳過，**不默默 default**）。transport（`process.cpp`）只負責 IO + 回調。

## 依賴

- 需要 `screen-fg` binary（同層 `../screen-fg/build/screen-fg` 或 `$SCREENFG_BIN`；完整順序見上）
- 需要 `../shared/`（純協議模組，與 `screen-fg` 共用；`-I <fgvk>/` 引用）
- screen-fg 端有對應的 additive 改動：event loop 讀 stdin、發 status/exit JSON、log 走 stderr、`--config` arg（不碰 LOCKED v1.0 管線）

## 陷阱（本機實測，gtkmm 4.20 / Ubuntu 26.04）

- **`Gtk::Application` 要 `add_window(*win)` 否則立刻退出**：只 `win->present()` 不够——app 不 track 該視窗，`run()` 立即回 0（進程活 <0.2s）。建窗後 `add_window(*win_)` 即正常保持運行。
- **reader 用 poll + chunked read（非逐字元 blocking read）**：reader 迴圈是 `pollFd`（100ms 超時）+ 8KB chunked `read` + `pending` buffer 抽行。這讓 destructor 能靠 `stopping_` 做 bounded 等待（reader ~100ms 內退出）。**不要**改回 blocking `read` 或逐字元 `readLine`（重用 buffer 若不 clear 會**累加**→ 解碼永遠抓到累加行的第一個 `"fps"`=0 → status 全顯示 `fps:0`）。每行**各別**交給純 `protocol::parse` 解碼。
- **destructor 不可阻塞 `join()`**：舊 code `reader_.join()` 會阻塞到子程序 stdout EOF——若子程序不退出（portal picker 中 / 卡在 present）app 退出會**卡死**（幾分鐘）。現行：`stopping_.store(true)` → reader ~100ms 內退出（bounded join）→ 關 stdin（子程序 EOF）→ `WNOHANG` waitpid（非阻塞）→ 子程序還活就 `kill(SIGTERM)` 確保收掉（否則 orphan 給 init）。
- **暫停/恢復 = re-exec（screen-fg 端）**：Vulkan layer 是 per-process 全域單例、config 只讀一次 → 無法運行期切換 → 暫停須 re-exec 整個 binary → **`source->start()` 會重跑 XDG portal picker（重新選窗）**；synthetic 模式看不到此現象。GUI 已在暫停按鈕 tooltip + log 提示。HUD 運行期狀態用 `SCREENFG_HUD` env 跨 re-exec 保留（`reexec(..., hudOn?"1":"0")`，re-exec'd process 啟動時讀回）。
- **gtkmm 4.0 API**：enum 全 scoped（`Gtk::Orientation::HORIZONTAL/VERTICAL`、`Gtk::Align::START/CENTER`、`Gtk::WrapMode::WORD_CHAR`、`Gtk::PolicyType::NEVER/AUTOMATIC`）；`Gtk::make_managed<T>()` 回 raw `T*`（**非** `Glib::RefPtr`，它是 shared_ptr-based 不接受 raw ptr）；`Gtk::Box::append(Widget&)` 只一個參（expand 要對 child `set_hexpand(true)`）；`Gtk::TextBuffer` 用 `get_bounds(begin,end)` + `insert`（回傳末端 iter）+ `scroll_to(iter, margin)`；`fs::canonical` 回 path（throw 非 optional）。
- **`Gtk::Switch` 訊號**：`signal_state_set()` 是 `SignalProxy<bool(bool)>`，`connect` 要**顯式**傳 `after` 參：`sig.connect([this](bool){ ...; return false; }, false)`。（`property_active().signal_notify()` 在此版不存在。）
- **多字元 UTF-8 勿用 `char` 字面量**：`find('（')` 會 multichar overflow（值被截斷）→ 改用字串 `find("（")`。
