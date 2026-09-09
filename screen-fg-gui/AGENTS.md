# screen-fg-gui

`screen-fg` 的 Linux 桌面 GUI 控制器（獨立程式，與 CLI 並存）。依 `../specs/screen-fg-gui/spec.md` 建置。

## Scope

GTK4（C++ 走 gtkmm-4.0；本機 libadwaita 無 C++ binding 故未連結，GNOME 預設 GTK4 主題即 Adwaita 故外觀相同）單一 binary `screen-fg-gui`：`fork`+`exec` 啟動 `screen-fg` 子程序、stdio 控制（命令走子程序 stdin、status JSON 走 stdout）、主窗戶提供 啟動/停止、狀態顯示（FPS/倍數/layer/狀態）、選窗（GUI 按鈕觸發 screen-fg 自己的 XDG portal picker）、執行中設定（HUD on/off、暫停/恢復）。不改 `screen-fg` 的 LOCKED v1.0 管線。

## Key files

- `CMakeLists.txt` — 建置（需 `libgtkmm-4.0-dev`；**不**需 libadwaita，無 C++ binding；`-I <fgvk>/` 引用 `shared/`；含純 `screen-fg-gui-tests` target）
- `src/main.cpp` — GTK 主窗戶（純 gtkmm：`Gtk::Application` subclass + `Gtk::Window`）+ binary 自動偵測；命令/狀態詞彙走 `shared/protocol.hpp`
- `src/process.{hpp,cpp}` — 子程序控制 **transport**（fork+exec、stdin/stdout/stderr 管線、寫命令、waitpid）；**解碼**（行 → status/exit）交給 `shared/protocol.hpp` 的純 `parse`
- `shared/protocol.hpp`（兄弟目錄 `../shared/`）— stdio 控制協議**單一來源**（與 `screen-fg` 共用）：`Message`、詞彙常數、純 emit/parse + 欄位存在性檢查
- `tests/test_protocol_decoder.cpp` — 純 ProtocolDecoder 測試（不 link gtkmm）
- `screen-fg-gui.desktop` — 桌面圖示

## 操作

- 建置：`cmake -B build && cmake --build build`
- 跑純 ProtocolDecoder 測試：`./build/screen-fg-gui-tests`（不 link gtkmm；依賴 `../shared/`）
- 執行：`./build/screen-fg-gui`（自動找 `screen-fg` binary：`$SCREENFG_BIN` → `../screen-fg/build/screen-fg` → PATH）
- 桌面圖示：把 `screen-fg-gui` 裝到 PATH 後，`cp screen-fg-gui.desktop ~/.local/share/applications/`

## 控制協議（與 screen-fg 的 stdio 通道）

**協議的單一來源是 `shared/protocol.hpp`**（兩端編同一份 header，詞彙/結構不可能漂移）：
- 命令（寫子程序 stdin，行式）：`pause` / `resume` / `hud 0|1` / `quit`（`protocol::Cmd`）
- status JSON（讀子程序 stdout）：`{"type":"status","fps":N,"mult":N,"layer":bool,"state":"running|paused|exiting"}`（`protocol::State`）
- exit JSON：`{"type":"exit","code":N}`
- 子程序 log 走 stderr（GUI 收進 log 區）
- **解碼是純函數** `protocol::parse(line) → optional<Message>`：缺欄/格式錯 → `nullopt`（明確跳過，**不默默 default**）。transport（`process.cpp`）只負責 IO + 回調。

## 依賴

- 需要 `screen-fg` binary（同層 `../screen-fg/build/screen-fg` 或 `$SCREENFG_BIN`）
- 需要 `../shared/`（純協議模組，與 `screen-fg` 共用；`-I <fgvk>/` 引用）
- screen-fg 端有對應的 additive 改動：event loop 讀 stdin、發 status/exit JSON、log 走 stderr、`--config` arg

## 陷阱（本機實測，gtkmm 4.20 / Ubuntu 26.04）

- **`Gtk::Application` 要 `add_window(*win)` 否則立刻退出**：只 `win->present()` 不够——app 不 track 該視窗，`run()` 立即回 0（進程活 <0.2s）。建窗後 `add_window(*win_)` 即正常保持運行。
- **reader 用 poll + chunked read（非逐字元 blocking read）**：reader 迴圈是 `pollFd`（100ms 超時）+ 8KB chunked `read` + `pending` buffer 抽行。這讓 destructor 能靠 `stopping_` 做 bounded 等待（reader ~100ms 內退出）。**不要**改回 blocking `read` 或逐字元 `readLine`（重用 buffer 若不 clear 會**累加**→ 解碼永遠抓到累加行的第一個 `"fps"`=0 → status 全顯示 `fps:0`）。每行**各別**交給純 `protocol::parse` 解碼。
- **destructor 不可阻塞 `join()`**：舊 code `reader_.join()` 會阻塞到子程序 stdout EOF——若子程序不退出（portal picker 中 / 卡在 present）app 退出會**卡死**（幾分鐘）。現行：`stopping_.store(true)` → reader ~100ms 內退出（bounded join）→ 關 stdin（子程序 EOF）→ `WNOHANG` waitpid（非阻塞）→ 子程序還活就 `kill(SIGTERM)` 確保收掉（否則 orphan 給 init）。
- **暫停/恢復 = re-exec（screen-fg 端）**：Vulkan layer 是 per-process 全域單例、config 只讀一次 → 無法運行期切換 → 暫停須 re-exec 整個 binary → **`source->start()` 會重跑 XDG portal picker（重新選窗）**；synthetic 模式看不到此現象。GUI 已在暫停按鈕 tooltip + log 提示。HUD 運行期狀態用 `SCREENFG_HUD` env 跨 re-exec 保留（`reexec(..., hudOn?"1":"0")`，re-exec'd process 啟動時讀回）。
- **gtkmm 4.0 API**：enum 全 scoped（`Gtk::Orientation::HORIZONTAL/VERTICAL`、`Gtk::Align::START/CENTER`、`Gtk::WrapMode::WORD_CHAR`、`Gtk::PolicyType::NEVER/AUTOMATIC`）；`Gtk::make_managed<T>()` 回 raw `T*`（**非** `Glib::RefPtr`，它是 shared_ptr-based 不接受 raw ptr）；`Gtk::Box::append(Widget&)` 只一個參（expand 要對 child `set_hexpand(true)`）；`Gtk::TextBuffer` 用 `get_bounds(begin,end)` + `insert`（回傳末端 iter）+ `scroll_to(iter, margin)`；`fs::canonical` 回 path（throw 非 optional）。
- **`Gtk::Switch` 訊號**：`signal_state_set()` 是 `SignalProxy<bool(bool)>`，`connect` 要**顯式**傳 `after` 參：`sig.connect([this](bool){ ...; return false; }, false)`。（`property_active().signal_notify()` 在此版不存在。）
- **多字元 UTF-8 勿用 `char` 字面量**：`find('（')` 會 multichar overflow（值被截斷）→ 改用字串 `find("（")`。
