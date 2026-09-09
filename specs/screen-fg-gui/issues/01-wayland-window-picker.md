# 01 Wayland 內嵌選窗可行性

Type: research
Status: resolved

## Question

「內嵌選窗」（取代 XDG portal picker）在本機（GNOME / gnome-shell / mutter，Wayland）是否可行？機制為何？

背景：screen-fg 現在用 XDG portal ScreenCast v5 讓使用者選一個瀏覽器視窗。這個 GUI 想要自己的「選視窗」UI，而不是系統對話框。
Wayland 安全模型一般不讓一個 client 列舉其他 client 的視窗 —— 調查在 GNOME Wayland 上實際能做什麼。

調查：
- Wayland client 能否列舉其他視窗供 picker UI 使用？
- 可用機制：XDG portal（現狀）、compositor 專屬 D-Bus（gnome-shell）、wlroots 系工具、grim / wl-list、XDG desktop portal。
- 有沒有 Wayland 原生「選視窗」讓 app 用（非通用 portal 對話框）？
- 可行選項 + 在 GNOME Wayland 上的可行性判定。

交付：可行性判定（可行 / 部分可行 / 不可行）＋ 機制 ＋ findings 寫成 markdown（存 `.scratch/screen-fg-gui/research/01-wayland-window-picker.md`）。

## Answer

**判定：GNOME Wayland / mutter 上「真正的 in-app 選窗」原生不可行**；唯一可行的是保留 XDG portal picker（GUI 的按鈕去觸發既有流程），或（v2/可選）自製 GNOME Shell extension 暴露 D-Bus「列舉/啟動視窗」API。

關鍵約束：mutter 無 in-process 視窗列舉、無「(x,y) 點到哪個視窗」hit-test；portal `Desktop` 物件沒有 `Pick` 方法（chooser 在 gnome-shell 內部）；`ext-foreign-toplevel-list-v1` / `wlr-foreign-toplevel-management` 在 mutter = ✗（wlroots/KDE 路徑不適用本機）。

完整 findings（含 13 個 source）：`.scratch/screen-fg-gui/research/01-wayland-window-picker.md`
