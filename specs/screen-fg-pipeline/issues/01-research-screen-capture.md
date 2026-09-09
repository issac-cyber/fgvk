# 01 Research: GNOME Wayland 程式化螢幕捕捉（window target）

Type: research
Status: resolved
Blocked by:

## Question

這台機器（Ubuntu 26.04、GNOME Wayland、xdg-desktop-portal + PipeWire 都在）上，一個**非 GUI 的 native 工具**要持續以高率（~90fps）捕捉**指定視窗**（瀏覽器），最優的程式化捕捉路徑是什麼？要釘死：

1. **可用 API**：xdg-desktop-portal screencast（DBus）、PipeWire client API（C / python binding / GStreamer）、或其他——哪一個給出 per-frame buffer 且 overhead 最低、控制力最大？
2. **Window-target 捕捉**：怎麼指定目標視窗（portal 的 window id/handle）？視窗移動/最小化/關閉時行為如何？
3. **Frame delivery model**：frame 是跟 VSync 走的嗎？能不能以高於 60Hz 的速率拿到 frame？每幀 latency 大概多少？（我們需要 ~90fps 餵給去重階段）
4. **Frame 內容**：同一個 source 幀被多次送出時，buffer 內容是像素級相同的嗎？（去重靠比對，需要確定性）portal/pipewire 會不會做後處理/scaling 導致「同一幀」出現差異？
5. **依賴**：C 庫 / python binding 分別要裝什麼？

Findings 寫進本檔 `## Comments`，再按 tracker 規則 resolve。

## Comments

### 0. 環境（本機實測）

- Ubuntu 26.04、GNOME Wayland、mutter 50.1、xdg-desktop-portal 1.21.1、xdg-desktop-portal-gnome 50.0、PipeWire/libpipewire 1.6.2、wireplumber、gstreamer1.0-pipewire 全部已裝；2× 3440×1440 顯示器 @ **167 Hz**（mutter DisplayConfig 實測）。
- **portal 1.21.1 的 bus name / object path 改了**：name 是 `org.freedesktop.portal.Desktop`（舊 `org.freedesktop.portal`），所有 interface 在 `/org/freedesktop/portal/desktop`（舊 `/org/freedesktop/portal`）。已 live introspect 確認 `org.freedesktop.portal.ScreenCast` 就導出在這裡；flatpak 官方 docs 仍寫舊 name（docs 落後於 binary）。
- **非 GUI 路徑已 live 驗證**：dbus-python（純 CLI 進程）呼叫 `ScreenCast.CreateSession` 成功拿到 session handle → 非 GUI native 工具走 portal D-Bus 完全可行。
- **抓到一個 1.21.1 的致命 bug**：`CreateSession` 若 options 沒有 `session_handle_token`，portal 整個 **abort / core dump**（assertion `session->token != NULL`，`src/xdp-session.c:300`；spec 說 token 可選「不給就自動生成」——1.21.1 違反 spec；新版 main branch 已改成回傳正常 error）。且 systemd 服務 **不會自動重啟**（`Type=dbus` + static，crash 後停在 `failed`）。實測中觸發過一次，手動 `systemctl --user restart xdg-desktop-portal` 救回。**規則：呼叫方必須自己生成 `session_handle_token`。**
- gnome-shell 的 `org.gnome.Shell.Introspect.GetWindows` 對一般呼叫者 **AccessDenied** → portal 是唯一合法的視窗列舉/捕捉路徑（順帶確認：直接 PipeWire client 或直連 mutter DBus 都拿不到 stream，必须走 portal）。
- gnome 的 picker/restore 後端是 portal-gnome 50.0 的 `screencast.c`/`gnomescreencast.c`/`shellintrospect.c`。

### 1. 可用 API（Q1）

- **權限閘門在 mutter**：GNOME Wayland 上 mutter 只在 `org.gnome.Mutter.ScreenCast` 上產生 screencast stream，且只接受來自 portal 後端（portal-gnome）的請求。所以非 GUI 工具**必須**走 portal 的 **ScreenCast** interface（注意：`RemoteDesktop` 是輸入控制，不是捕捉，別混）。
- 流程（本機 live 驗證的 **v5**，`AvailableSourceTypes=7`、`AvailableCursorModes=7`）：
  `CreateSession(options{session_handle_token}) → SelectSources(session, options{types, persist_mode, restore_data?}) → Start(session, parent_window, options{cursor_mode,...}) → [response: streams + restore_data] → OpenPipeWireRemote(session) → fd`
- **portal 只是 proxy，mutter 是 producer**。取流的乾淨方式是 `OpenPipeWireRemote`：response 直接給一個**預先開好的 PipeWire client fd** → `pw_client_from_fd()`。（次選：`Start` response 每支 stream 有 `pipewire-node` ID，普通 PW client 可照 ID subscribe；較不乾淨。）
- **PipeWire client API（libpipewire-0.3, C）= per-frame buffer + 完整 SPA meta（PTS/seq/VideoCrop/VideoDamage/size），overhead 最低、控制力最大** → 主選。
- GStreamer `pipewiresrc`（套件已裝）= 可行快捷路徑（`pipewiresrc ! videoconvert ! appsink`），少一層 meta 控制、多一層 pipeline——適合 prototype，不適合最終版。
- Python 組合：dbus-python（已裝，portal 端）+ python3-pipewire / pypewire（stream 端）。
- 出處：ScreenCast spec https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html ；portal 來源 https://github.com/flatpak/xdg-desktop-portal （tag 1.21.1 `src/screen-cast.c`、`src/xdp-session.c`）；portal-gnome https://gitlab.gnome.org/GNOME/xdg-desktop-portal-gnome ；PipeWire docs https://docs.pipewire.org/page_streams.html

### 2. Window-target 捕捉（Q2）

- `SelectSources types=2 (WINDOW)`：第一次會彈 **picker 對話框**（一次性的 user action）；response 給 `restore_data`（含 app_id+title 指紋）+ streams。之後跑法帶 `persist_mode=1` + `restore_data` 可**跳過對話框**。
- portal-gnome 後端用 `Shell.Introspect.GetWindows` 把選中的視窗解析成 **stable shell window id (uint64)** 傳給 mutter 的 `RecordWindow`。
- 視窗狀態行為（mutter 50.1 源碼確認）：
  - **移動** → 每幀 `VideoCrop` meta 更新；window stream 是**整片 monitor 大小的 canvas**，consumer 必須每幀照 VideoCrop 重新 crop。
  - **最小化** → stream 保持開啟，但 frame 停止（event-driven，沒有 redraw 就沒有 frame）。
  - **關閉** → stream 被銷毀；consumer 要處理 stream 消失並用同一份 `restore_data` 重新 `Start`。
  - **restore 匹配是 fuzzy**（app-id 相同 + title 相似距離）——分頁 title 改太多會匹配失敗 → 對話框重現。長期跑的 pipeline 要把「對話框重現」列為 failure mode。

### 3. Frame delivery model（Q3）——最重要的一條

- **Window stream：event-driven（只在 damaged/redraw 時出幀）+ 硬性 60fps cap**（mutter `meta-screen-cast-window-stream-src.c`：spec `frame_rate = 60.0f`、min-interval gate 丟掉过快幀、16.67ms timer 是 retry/backoff 而非 periodic pump）。靜止頁面 → 0 frame；瀏覽器放片 → ≤60fps。**window target 拿不到 ~90fps，連「靜態時持續 60fps」都不存在。**
- **Monitor stream：掛 `stage_painted`（每個 compositor frame / VSync）→ 全幀，速率 = 顯示器 refresh rate（本機 167Hz）**——唯一能 >60Hz 的路徑。
- **要 ~90fps 的唯一做法：抓 monitor（`types=1`）+ client-side crop 出瀏覽器視窗的 rectangle**（cursor HIDDEN；視窗 rect 用 window stream 的 VideoCrop/position props 或 Shell introspection 追蹤）。
- 每幀 latency：event → pipeline，通常個位數 ms。
- **對 spec 的衝擊（map decisions 要改）**：「高率捕捉 ~90fps + portal window target」照原樣不可行；要改成 monitor 捕捉 + client crop（或把 window target 降級成 60fps + event-driven）。去重階段本來就吃重複幀，event-driven 對它是免費的。
- 出處：mutter 50.1 源碼 https://gitlab.gnome.org/GNOME/mutter （`src/backends/meta-screen-cast-window-stream-src.c`、`meta-screen-cast-monitor-stream-src.c`、`meta-screen-cast-stream-src.c`：`DEFAULT_FRAME_RATE SPA_FRACTION(60,1)`、`max_framerate` gate）

### 4. Frame 內容確定性（Q4）

- 一幀 = compositor 現存 buffer 的**直接 1:1 紋理讀取**：mutter → PipeWire 路徑**沒有 scaling、dithering、post-processing**；cursor 只在 `cursor_mode=EMBEDDED` 時畫（我們用 HIDDEN）。native 格式 BGRA/BGRX 8888。
- 所以：同一 source 內容被記錄兩次 → **像素級相同**（同一張紋理兩次讀出，無任何隨機性）。「同一幀被多次送出」（60fps gate 前的重複 / 重試）= 同一內容 = 相同。
- 去重比對要**在 native 格式上做**（raw bytes 的 sha256，或粗粒度 tile hash），**先 crop 再 hash**（VideoCrop 給的整數 rect），**比對前不要做格式轉換**（GStreamer `videoconvert` 會 dither/scale 破壞確定性）。
- 注意區分：兩次捕捉之間源內容「真的變了」（局部 redraw）是 content change，不是非確定性——去重靠比對自然處理。

### 5. 依賴（Q5）

- **已裝**：pipewire 1.6.2（libpipewire-0.3-0）、xdg-desktop-portal 1.21.1、xdg-desktop-portal-gnome 50.0、wireplumber、gstreamer1.0-pipewire（pipewiresrc）、python3-gi、dbus-python 1.4.0、mutter 50.1。
- **C build 要補**：`sudo apt install libpipewire-0.3-dev libglib2.0-dev`（PW headers + GDBus headers；目前都未裝）。
- **Python 要補**：`sudo apt install python3-pipewire`（或 venv + `pip install pypewire`——系統 pip 被 PEP 668 擋）。
- **GStreamer 路徑**：不用裝東西（pipewiresrc 已在）。
- dbus 呼叫端：dbus-python（已裝）或 python3-gi 的 Gio（已裝，注意老版 GLib.Variant constructor 有 bug，複雜 variant 用 dbus-python 或 VariantBuilder）。

## Answer

1. **API**：xdg-desktop-portal **ScreenCast v5**（DBus；本機注意 name=`org.freedesktop.portal.Desktop`、path=`/org/freedesktop/portal/desktop`）做權限/建立 stream，**PipeWire client API 走 `OpenPipeWireRemote` 拿到的 fd** 做 per-frame 取流（libpipewire-0.3, C；GStreamer pipewiresrc 是 prototype 快捷路徑）。必帶 `session_handle_token`（1.21.1 不帶會把 portal 打掛）。
2. **Window targeting**：`SelectSources types=WINDOW` + `persist_mode` + `restore_data`（首跑要點一次 picker）；stream 是 monitor-sized canvas，每幀照 `VideoCrop` crop；移動=重新 crop、最小化=frame 停、關閉=stream 消失要重 `Start`；restore 靠 app_id+title fuzzy 匹配（title 大改會失敗）。
3. **Frame delivery**：window stream 是 **event-driven（只 redraw 才出幀）且硬性 60fps cap** → window target **拿不到 90fps**；monitor stream 跟 VSync 走、可到 **167Hz**。要 ~90fps：**抓 monitor + client-side crop 視窗 rect**（cursor HIDDEN）。每幀 latency 個位數 ms。
4. **確定性**：native BGRA 下**像素級相同**（1:1 紋理讀取、無 scaling/dithering、cursor HIDDEN）；去重在 native 格式 + 整數 crop 後做 raw hash，比對前禁止格式轉換。
5. **依賴**：C 補 `libpipewire-0.3-dev` + `libglib2.0-dev`；Python 補 `python3-pipewire`（或 venv + pypewire）；其餘（portal/pipewire/pipewiresrc/dbus-python）已就位。
