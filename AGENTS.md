# fgvk

fg（frame generation）專案：**影片的即時插幀**（本機個人用）。機器：Ubuntu 26.04 / GNOME Wayland / 2× AMD R9700（各帶一屏 3440×1440）/ lsfg-vk 2.0.0 已裝（shader container 已恢復）。

## 現況（FG 路線結論）

- **lsfg-vk FG 引擎 = 活的**（`lsfg-vk-cli benchmark` 2× 正常）。
- **Chromium 瀏覽器內 FG = 死**：瀏覽器合成器以顯示 refresh（165/174Hz）present → FG 被 cap → 無效。
- **screen-fg portal 捕捉 = 死（dead end）**：source 出一幀後 freeze（PipeWire buffer pool 死鎖，xdpw#395）。原 `screen-fg`／`screen-fg-gui`／`shared` 組件與 `specs/screen-fg-*` 已**移出本 repo**（2026-09-11）；設計史留在 `docs/`＋`explainer.html`。
- **可用的 FG 路線 = mpv 外部視窗 + lsfg-vk**：`mpv --vo=gpu --gpu-api=vulkan`（vsync pacing swapchain）→ layer 攔截 → 插幀生效。`youtube-fg-extension/` 走這條。

## Scope

repo 現為「可用的 FG 路線」＋設計/歷史記錄：

| 組件 | 內容 |
|------|------|
| `youtube-fg-extension/` | **可用的 FG 路線**（Chrome extension + native messaging host）：popup 2x/3x/4x/10x → host `fgvk-mpv-launch.py` → `mpv --vo=gpu --gpu-api=vulkan` + `LSFGVK_PROFILE` + `MESA_VK_DEVICE_SELECT`（GPU1）；background service worker 監控 mpv、mpv 被關掉自動還原來源 tab；支援 anime1.me + hanime1.me。見 `youtube-fg-extension/AGENTS.md` |
| `docs/` | 設計/建置計畫記錄（dated record，含 screen-fg 建置之歷史＋extension 設計）；不改寫歷史 |
| `explainer.html` | 專案解說頁（screen-fg 管線的歷史解說；standalone HTML） |

## 建置 & 驗證

| 做什麼 | 命令 |
|--------|------|
| extension host selftest | `youtube-fg-extension/host/fgvk-mpv-launch.py --selftest` |
| extension 單元測試（node） | `cd youtube-fg-extension && node test/url-utils.test.cjs` |
| profile 選得到 | `LSFGVK_PROFILE="2x FG / 100%" mpv --vo=gpu --gpu-api=vulkan <clip>` → log 有 `Using profile '2x FG / 100%' (identified via environment)` |
| extension e2e（手動驗證） | 完全重啟 Chrome → youtube.com → 點擴充圖示 → 2x → 看 mpv 視窗；popup「Last launch」顯示成功/失敗 |

- layer 前置：Steam Lossless Scaling 保持在 `lsfg-vk` update channel
- 系統依賴：mpv + yt-dlp（已裝）；extension 的 native host 是純 python（stdlib 即可、無第三方套件）

## 配置 & 運行期檔案

| 檔案 | 用途 |
|------|------|
| `~/.config/lsfg-vk/conf.toml` | lsfg-vk layer 設定；現 4 支 FG profile（2x/3x/4x/10x / 100%，multiplier 2/3/4/10，vsync + override_present_mode + performance_mode）；extension 用 `LSFGVK_PROFILE` 選 profile（不需 active_in）。**⚠️ lsfg-vk GUI（Lossless Scaling app）存檔時會覆蓋此檔**——GUI 裡存過檔會把這 4 支 profile 清掉，要在 GUI 內重新加（或別在 GUI 存檔）；備份 `conf.toml.bak-*` |
| `~/.local/share/vulkan/implicit_layer.d/` | lsfg-vk implicit layer（`VK_LAYER_LSFGVK_frame_generation`）位置；未 enumerate 到時 host/health 警告 |
| `~/.config/fgvk/` | extension host 運行期目錄：mpv PID（`mpv.pid`）＋ mpv log |
| `~/.local/bin/fgvk-mpv-launch.py` | extension native host（安裝位置，chmod +x） |
| `~/.config/google-chrome/NativeMessagingHosts/com.fgvk.host.json` | extension native host manifest（`allowed_origins` = extension ID `jggfcgmdjhdeokfjlmnfdmnekodggdjc`） |

## 環境變數速查

| 變數 | 使用方 | 用途 |
|------|--------|------|
| `LSFGVK_PROFILE` | lsfg-vk layer | 用名稱選 profile（extension host 用它設 2x/3x/4x/10x；不需 active_in） |
| `MESA_VK_DEVICE_SELECT` | Mesa device-select layer | 固定特定 GPU（extension host 用 `1002:7551:0000:07:00.0` = GPU1／第二張 R9700；兩張 R9700 同 ID 1002:7551，須用 PCI BDF 區分） |
| `DISABLE_LSFGVK` | lsfg-vk layer | `"1"` = 關掉 layer |

## 命名

`fgvk` 是專案/目錄名；extension 的 native host 叫 `fgvk-mpv-launch.py`。搜尋 code 搜 `fgvk`／`mpv-launch`（`screen-fg` 組件已移出）。
