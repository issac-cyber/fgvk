# youtube-fg-extension

**可用的 FG 路線**：Chrome extension + native messaging host，點一下就開 `mpv`（Vulkan）外部視窗 + `lsfg-vk` 插幀看**網頁影片**（YouTube / Bilibili / 任何 yt-dlp 支援的站；DRM 站會失敗）。瀏覽器本身不插幀（Chromium 內 FG 被 refresh cap 死，見頂層 AGENTS.md 現況）；本 extension 是把「可 work 的 mpv+lsfg-vk」包成一個可點的方案。mpv 靠 `yt-dlp`（已裝）自動解析串流站網址。

## 運作

popup 選 2x/3x/4x/10x → `chrome.runtime.sendNativeMessage("com.fgvk.host", {url, multiplier})` → host `fgvk-mpv-launch.py` 發 `mpv --vo=gpu --gpu-api=vulkan <url>`，帶 `LSFGVK_PROFILE=<N>x FG / 100%` + `MESA_VK_DEVICE_SELECT=1002:7551:0000:07:00.0`（GPU1／第二張 R9700）。

**音頻**：啟動時先靜音來源 tab（`chrome.tabs.update {muted:true}`，tab id 記在 `chrome.storage`）、mpv 播音（單一播放器，音畫同步最佳）。**暫停來源影片**：啟動時用 `chrome.scripting`（靠 `activeTab`，點圖示即授予）把頁上 `<video>` `pause()`、停止時 `play()` 恢復（省串流/CPU、避免雙重畫面）。**視窗管理**：「停止」鈕 → `{stop:true}` 殺 mpv＋解除 tab 靜音＋恢復影片；新一次啟動會先殺上一支 mpv（replace，mpv PID 持久化在 `~/.config/fgvk/mpv.pid`，含等舊的退出釋放 GPU）。後台 tab 不玩（省電）。

## Key files

| 檔案 | 用途 |
|------|------|
| `manifest.json` | MV3；`tabs` + `nativeMessaging` + `storage` + `scripting` 權限；`icons` + `action.default_icon`（像素小鴨） |
| `icons/` | extension 圖示（像素小鴨，16/32/48/128，透明背景） |
| `popup.html` / `popup.js` | 2x/3x/4x/10x +「停止」鈕；啟動時靜音來源 tab＋暫停 `<video>`、停止時解除＋恢復；連 native host；顯示成功/失敗 |
| `host/fgvk-mpv-launch.py` | native messaging host（讀 stdio length-prefixed message → `launch`/`stop`；mpv PID 持久化在 `~/.config/fgvk/mpv.pid`；新啟動 replace 先殺舊的＋等舊的釋放 GPU）。`--selftest` 可離線驗 |
| `host/com.fgvk.host.json` | host manifest（`path` 指向 `~/.local/bin/fgvk-mpv-launch.py`；`allowed_origins` = extension ID） |
| `url-utils.js` | `isVideoUrl` + `multiplierToProfile`（純函式，popup 與 host 同源語法；放寬：任何 http(s) 網頁皆可，mpv+yt-dlp 解析） |
| `test/url-utils.test.cjs` | node 單元測試 |
| `install.md` | 安裝步驟 |
| `docs/superpowers/{specs,plans}/2026-09-10-youtube-fg-extension*.md` | 設計 spec + 實作 plan |

## 安裝（一次性）

1. host 放到 `~/.local/bin/fgvk-mpv-launch.py`（chmod +x）；`host/com.fgvk.host.json` 放到 `~/.config/google-chrome/NativeMessagingHosts/`（`allowed_origins` 填 extension ID `jggfcgmdjhdeokfjlmnfdmnekodggdjc`）。
2. Chrome `chrome://extensions` → 開發者模式 → 載入未封裝 → 選本資料夾。
3. **完全重啟 Chrome**（native host manifest 只在啟動時讀）。

## 驗證

| 做什麼 | 命令 / 步驟 |
|--------|------|
| host selftest | `host/fgvk-mpv-launch.py --selftest` |
| 單元測試 | `node test/url-utils.test.cjs` |
| profile 選得到 | `LSFGVK_PROFILE="2x FG / 100%" mpv --vo=gpu --gpu-api=vulkan <clip>` → log 有 `Using profile '2x FG / 100%' (identified via environment)` |
| e2e | 重啟 Chrome → youtube.com 任意影片 → 點圖示 → 2x → mpv 視窗插幀 → 點「停止」→ mpv 關閉＋tab 解除靜音 |

## 取決 / 前置

- lsfg-vk 2.0.0（`~/.local/share/vulkan/implicit_layer.d/VK_LAYER_LSFGVK_frame_generation.json` 已存在、layer 已 hook）。
- conf.toml 有 4 支 FG profile（見頂層 AGENTS.md 配置表）；`--selftest` 會檢查 `LSFGVK_PROFILE` 是否可用。
- GPU 固定 GPU1（兩張 R9700 同 ID，用 PCI BDF 區分）；要換 GPU0 改 host 的 `GPU_SELECT` 常數為 `0000:03:00.0`。

## 陷阱

- extension ID 要靠**金鑰**鎖定（`manifest.json` 的 `key`）才會是固定的 `jggfcgmdjhdeokfjlmnfdmnekodggdjc`；改 key 或移除會讓 ID 變 → NativeMessagingHosts 的 `allowed_origins` 要跟著改。
- native host 要 `chmod +x`，且 `allowed_origins` 的 extension ID 要正確，否則 `connectNative` 回連不上。
- GPU 用 `MESA_VK_DEVICE_SELECT` 固定；兩張 R9700 device id 相同，只用 `1002:7551` 無法區分，要帶 PCI BDF。
- mpv PID 持久化在 `~/.config/fgvk/mpv.pid`；`read_pid()` 殺前會驗證 process 存活**且** `comm==mpv`（防 PID 重用殺錯），所以殘留的舊 PID 無害。
