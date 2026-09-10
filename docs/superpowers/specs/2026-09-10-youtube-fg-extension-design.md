# YouTube FG Extension — 設計規格

- 日期：2026-09-10
- 狀態：設計已確認，待實作

## 目標

一個 Chrome 擴充套件：在 YouTube 影片頁按一下，就把該影片用 `mpv` + lsfg-vk frame generation（FG）開起來，並可選 **2x / 3x / 4x** 倍率。讓「看 YouTube 補幀」變成一步動作。

## 背景與約束（本專案已驗證的事實，非重複調查）

- FG **無法發生在瀏覽器內**：Chromium 合成器以螢幕 refresh（本機 165/174Hz）present，lsfg-vk（Hook Vulkan present）抓不到影片幀率去補幀。
- **mpv + lsfg-vk 的 FG「通」**（使用者 2026-09-10 已確認）：`mpv --vo=gpu --gpu-api=vulkan <url>` 走 lsfg-vk 2.0.0 implicit layer 補幀。
- 本機：YouTube 由 mpv+yt-dlp 播放（av1 60fps 正常）。mpv v0.41.0、yt-dlp 已裝。
- lsfg-vk profile 選取：設 `LSFGVK_PROFILE="<profile name>"` 環境變數即可指定（跳過 `active_in` 比對）。
- Chrome 以 Native Messaging 與外部程式溝通（Bitwarden 正在用同一機制；host manifest 路徑 `~/.config/google-chrome/NativeMessagingHosts/`）。

## 元件（4 個）

| 元件 | 內容 | 位置 |
|------|------|------|
| Chrome 擴充套件 | MV3，popup 有 2x/3x/4x 按鈕；送 Native Messaging 訊息 | fgvk 內新目錄 `youtube-fg-extension/` |
| Native Messaging host | bash script：讀 JSON、驗證、background 啟動 mpv + FG | 原始碼 `youtube-fg-extension/host/`，安裝到 `~/.local/bin/fgvk-mpv-launch.sh` |
| host manifest | JSON，註冊 host + 綁 extension ID | `~/.config/google-chrome/NativeMessagingHosts/com.fgvk.host.json` |
| lsfg-vk profiles | `2x / 3x / 4x FG / 100%` 三個 profile（multiplier 2/3/4） | `~/.config/lsfg-vk/conf.toml` |

## 資料流（按一顆按鈕）

1. 使用者點 popup 的「3x」。
2. extension 讀目前 active tab 網址；驗證 host 是 `www.youtube.com` 且 path 是 `/watch`（否則報「此頁不是 YouTube 影片」）。
3. extension 發 Native Messaging 訊息 `{"url":"https://www.youtube.com/watch?v=...","multiplier":3}`。
4. host 讀 length-prefixed JSON → 驗證 `multiplier ∈ {2,3,4}` 且 `url` 是 `https://www.youtube.com/…` → 映射 profile 名（`Nx FG / 100%`）→ 以 background（detached、脫離 stdin）執行：
   ```
   LSFGVK_PROFILE="3x FG / 100%" mpv --vo=gpu --gpu-api=vulkan "<url>"
   ```
5. host 回 ack `{"ok":true}`；extension popup 顯示「已啟動（3x）」。

## Native Messaging 協議

- 標準 Chrome Native Messaging：stdin/stdout，訊息 = 4-byte native-endian 長度 + JSON。
- request（extension → host）：`{"url": string, "multiplier": int}`
- response（host → extension）：`{"ok": true}` 或 `{"ok": false, "error": string}`

## lsfg-vk profiles

在 `~/.config/lsfg-vk/conf.toml` 加/確保三個 profile（名字與 host 用 `LSFGVK_PROFILE` 對應）：

| name | multiplier | flow_scale | performance_mode |
|------|-----------|-----------|------------------|
| `2x FG / 100%` | 2 | 1.0 | false |
| `3x FG / 100%` | 3 | 1.0 | false |
| `4x FG / 100%` | 4 | 1.0 | false |

（現有 `4x FG / 85% [Performance]` 保留不動；`3x` 為新增。）

## 錯誤處理

- 非 YouTube watch 頁 → popup：「此頁不是 YouTube 影片」。
- Native Messaging 連線失敗（host 未裝 / manifest 缺）→ popup：「未安裝 host」＋一次性安裝說明。
- mpv / yt-dlp 缺 → host 以 `command -v` 檢查、回 `{"ok":false,"error":"mpv 未安裝"}`。
- host 執行失敗（bad URL / bad multiplier）→ 回對應 error、不崩。

## 一次性安裝

1. 載入 unpacked extension（chrome://extensions → Developer mode → Load unpacked）。
2. 放好 host script（`chmod +x`）+ host manifest（`path` 填 script 絕對路徑）。
3. 把 extension ID 寫進 host manifest 的 `allowed_origins`（`["chrome-extension://<ID>/"]`）。
4. `conf.toml` 確保三個 profile 在。

## 不做（YAGNI / 非目標）

- 不做瀏覽器內 FG（技術上不可能，見背景）。
- 不做控制 mpv（暫停/快轉/音量）—— 只管「啟動」。
- 不做非 YouTube 網站。
- 不做倍率以外的 FG 參數（flow_scale / performance_mode 用固定預設）。
- 不做自動更新/上架 Chrome Web Store（unpacked 個人用）。
