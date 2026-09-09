# 02 GUI toolkit 可行性

Type: research
Status: resolved

## Question

獨立桌面控制 App 該用哪個 GUI toolkit？本機（Ubuntu 26.04，GNOME Wayland）有什麼可用 / 實務？

調查：
- 已安裝 / 易裝：GTK4、Qt6、SDL3（screen-fg 已在用）、其他（FLTK、Dear ImGui、web/Electron）。
- 各別：dev package 是否可用、對控制 App（widgets / 設定 UI / 狀態顯示）的契合度、Wayland 支援。
- 推薦最適合「控制 App（啟動 / 控制 C++ screen-fg 程序）」的那個。

交付：可用性清單 ＋ 推薦 toolkit ＋ 理由（存 `.scratch/screen-fg-gui/research/02-gui-toolkit.md`）。

## Answer

**推薦：GTK4 + libadwaita（C++ 走 gtkmm-4.0）**；Qt6 次選。

本機 runtime 全在（GTK4 4.22.4 + libadwaita 1.9.1、Qt6 6.10.2 + wayland 平台主題、SDL3）；dev 頭只有 SDL3 已裝，GTK4/Qt6 dev 一 `apt install` 即得（g++ 15.2 + cmake 就緒）。GTK4 是 GNOME HIG 正牌 toolkit —— 原生外觀、widget 現成、依賴最小（2–3 個 dev package）。選窗（01）/IPC（03）與 toolkit 解耦。

完整 findings：`.scratch/screen-fg-gui/research/02-gui-toolkit.md`
