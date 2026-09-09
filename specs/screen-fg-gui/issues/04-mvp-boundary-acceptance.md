# 04 MVP 界線與驗收標準

Type: grilling
Status: resolved
Blocked by: 01, 02

## Question

MVP 含全部四功能（啟動/停止、狀態顯示、內嵌選窗、執行中調設定 — 已確認）。決定具體界線與驗收標準：

- 版本界線：v1 最小閉環要做出什麼、GUI 依賴哪些 screen-fg 側 interface
- 驗收標準（definition of done）：每個功能怎麼驗（含 e2e）
- 依賴 01（選窗可行性）與 02（toolkit）：若選窗不可行 / toolkit 受限，界線怎麼調整
- 命名 / 打包：GUI 程式叫什麼、怎麼安裝 / 啟動

本 ticket 等 01、02 的可行性結論出來才能定界線。（HITL：`grilling` + `domain-modeling`。）

## Answer

**v1 界線（已確認）**
- 獨立程式 **`screen-fg-gui`**（GTK4 + libadwaita / gtkmm，依 02），binary + `.desktop` 圖示、不自啟。
- 4 功能：
  1. **啟動/停止** — 按鈕啟動/停止 screen-fg；GUI 顯示 running 狀態
  2. **狀態顯示** — 即時 FPS ／ 倍數 ／ layer 狀態
  3. **選窗** — GUI 按鈕觸發**既有 XDG portal picker**（in-app 原生選窗依 01 不可行）→ 選窗 → 啟動捕捉
  4. **執行中調設定** — HUD ／ 暫停，立即生效、不重啟
- GUI 不改 screen-fg 內部管線（LOCKED v1.0）；依賴 screen-fg 暴露「啟動參數＋執行中命令＋狀態」interface（機制 → ticket 03）。

**驗收標準（definition of done）**
- e2e：GUI 啟動 → 選窗 → 捕捉 → 全螢幕呈現 → lsfg-vk 插幀 → 乾淨停止（exit 0），全通
- 啟動/停止：啟動鈕→程序起來＋呈現出現；停止鈕→乾淨退出；狀態顯示正確
- 狀態：FPS／倍數／layer 狀態在 GUI 即時更新
- 選窗：按鈕→portal 彈出→選窗→捕捉啟動
- 調設定：HUD／暫停立即生效、不重啟
- 安裝：`screen-fg-gui` 進 PATH＋`.desktop` 圖示，可點選啟動
