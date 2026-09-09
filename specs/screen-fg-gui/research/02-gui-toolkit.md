# 02 GUI Toolkit 可行性

Type: research
Status: resolved
Date: 2026-09-09

## Question

本機（Ubuntu 26.04、GNOME Wayland / gnome-shell / mutter、mesa radv）上有哪些 GUI toolkit 可用 / 易裝？哪一個最適合做「啟動/控制現有 C++ screen-fg process 的獨立控制 App」（按鍵、設定 UI、狀態顯示、內嵌選窗）？

## 本機環境事實（local checks）

- `XDG_SESSION_TYPE=wayland`，GNOME/mutter 合成器，`gnome-shell` 存在。
- 編譯工具鏈齊全：`g++ 15.2`、`cmake 4.3.2`、`ninja 1.12`、`pkg-config`；`meson` 缺席（但 GTK/Adwaita 用 cmake 或 pkg-config 即可）。
- `node v24` / `npm` 存在；`electron` / `flutter` 未裝。
- 運行時库已裝：GTK4（`libgtk-4-1 4.22.4`）+ `libadwaita-1-0 1.9.1` + `adwaita-icon-theme 50.0`；Qt6（`libqt6widgets6/gui6/core6 6.10.2` + `qt6-wayland` + `qt6-gtk-platformtheme`）；SDL3（`libsdl3-0`）。
- **唯 SDK 頭已裝的是 SDL3**（`libsdl3-dev`，`/usr/include/SDL3`）。GTK4 / Qt6 的 dev 頭未裝，但候選包都在（一 `apt install` 即得）。
- 無現成選窗輔助工具：`wl-list` / `wlr-list` / `slurp` / `xdotool` / `grim` 全缺席 → 選窗（ticket 01）是真正硬點，且與 toolkit 解耦。

## Availability table

| Toolkit | Installed? (runtime/dev) | Dev pkg (apt) | Wayland | C++ | Fit for control app |
|---|---|---|---|---|---|
| **GTK4 + libadwaita** | runtime ✓ / dev ✗ | `libgtk-4-dev`、`libadwaita-1-dev` 可裝（候選在）；C++ 走 `libgtkmm-4.0-dev`（4.20.0）可裝 | 一級原生（GNOME/mutter） | ✓（gtkmm 或 C/GObject） | **最佳**：原生 HIG widget 全備（按鍵/combo/slider/list/headerbar/toast/status） |
| **Qt6 (Widgets / QML)** | runtime ✓ / dev ✗ | `qt6-base-dev`、`qt6-declarative-dev` 可裝（6.10.2） | 成熟（`qt6-wayland` 已裝；`qt6-gtk-platformtheme` 給 GNOME 原生感外觀） | ✓（主場） | 強 #2：widget 豐富、GNOME 上可近原生 |
| **SDL3** | runtime ✓ + **dev ✓**（頭已裝） | 已裝 | ✓（Wayland backend） | ✓ | 差：多媒體库、無 widget，要手寫或加 ImGui |
| **FLTK 1.3.11** | runtime ✗ | `libfltk1.3-dev` 可裝（1.3.11） | 1.3.11 部分/實驗（可關 Wayland 退回 X11） | ✓（C++ 原生） | 差：本機版 Wayland 不完整、widget 偏舊 |
| **Dear ImGui** | runtime ✗ | `libimgui-dev` 可裝（1.92.2b） | 經 backend（SDL/GLFW）才得 | ✓ | 差於設定 App：無原生外觀/視窗框；適作內嵌 HUD |
| **Tk** | runtime ✗ | `tk-dev` 可裝（8.6.16）；`python3-tk` 可裝 | 有限（X11 為主） | 弱（C via Tcl） | 差：舊、X11 優先 |
| **Electron / web** | node ✓ / electron ✗ | npm 可裝（重 ~150–300 MB） | ✓（Chromium） | ✗（JS） | 過度：依賴膨脹大 |

## Recommendation

**GTK4 + libadwaita**（C++ 用 `gtkmm-4.0`，或 C/GObject 亦可）。
Strong runner-up：**Qt6**（mature Wayland、原生 C++、經 `qt6-gtk-platformtheme`/`qgnomeplatform` 近原生外觀）。

## Rationale

1. **原生 GNOME Wayland 外觀** — libadwaita 就是 GNOME 的 HIG toolkit；本機正是 GNOME Wayland。零 X11 fallback 風險，長得像原生 GNOME 應用（照 HIG 給 headerbar、sidebar、toggle、combo、toast、status row）。
2. **widget 現成** — 本 app 需要的全是「按鍵、設定 UI、狀態顯示、list/選窗」，GTK4/Adwaita 預備齊全，不用自己畫。
3. **依賴膨脹最小** — 只要 2–3 個 dev 包（`libgtk-4-dev` + `libadwaita-1-dev`〔+ `libgtkmm-4.0-dev` 若要 C++〕）；runtime 库**本機已全在**，裝 dev 頭即可編。
4. **C++ 支援** — `gtkmm-4.0`（可裝）讓 app 與 screen-fg 同為 C++；也可用 C + GObject。
5. **硬點與 toolkit 解耦** — 啟動/控制 C++ screen-fg binary（subprocess + IPC，ticket 03）與內嵌選窗（ticket 01）都不依賴 toolkit；本機無現成選窗工具，選窗將走 helper process / D-Bus / portal 複用，結果餵給 GUI 的 list + 按鍵即可。GTK 只負責 render，不會被選窗機制鎖死。

**為何不選 SDL3（雖 dev 已裝）：** 它是多媒體/輸入库，無 widget 系統；要手寫 UI 或加 ImGui 才成本反而高，且失去原生 GNOME 外觀。
**為何不選 Qt6 作首選：** 差異不在能力（都很強），而在「原生 GNOME 外觀」與「最小依賴」兩點上 GTK/Adwaita 更貼（本機 = GNOME，非 Plasma）。

## 對 MVP 四功能的影響

| 功能 | 在 GTK4/Adwaita 上 |
|---|---|
| 啟動/停止 | `Gtk::Button` + `Glib::spawn` / `Glib::spawn_async` 起/停 screen-fg；IPC 走 ticket 03 的通道 |
| 狀態顯示 | 直接 widget（label / progress / 狀態列）綁定 IPC 回傳 |
| 內嵌選窗 | GTK 渲染 list + 選中；picker 機制（ticket 01）與 toolkit 解耦 |
| 執行中調設定 | combo / toggle / slider 發 IPC 熱套 |

## Sources

- Local: `pkg-config --list-all`、`dpkg -l`、`apt-cache policy`、`/usr/include`（本機直接查）
- FLTK 1.3.11 Wayland（X11 fallback 限制）: https://github.com/fltk/fltk/releases
- libadwaita = GNOME HIG toolkit: https://gnome.pages.gitlab.gnome.org/libadwaita/ ；https://gtk-rs.org/gtk4-rs/stable/latest/book/libadwaita.html
