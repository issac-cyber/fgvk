# youtube-fg-extension

**可用的 FG 路線**：Chrome extension + native messaging host，點一下就開 `mpv`（Vulkan）外部視窗 + `lsfg-vk` 插幀看**網頁影片**（YouTube / Bilibili / 任何 yt-dlp 支援的站；DRM 站會失敗）。瀏覽器本身不插幀（Chromium 內 FG 被 refresh cap 死，見頂層 AGENTS.md 現況）；本 extension 是把「可 work 的 mpv+lsfg-vk」包成一個可點的方案。mpv 靠 `yt-dlp`（已裝）自動解析串流站網址。

## 運作

popup 選 2x/3x/4x/10x → `chrome.runtime.sendNativeMessage("com.fgvk.host", {url, multiplier})` → host `fgvk-mpv-launch.py` 發 `mpv --vo=gpu --gpu-api=vulkan <url>`，帶 `LSFGVK_PROFILE=<N>x FG / 100%` + `MESA_VK_DEVICE_SELECT=1002:7551:0000:07:00.0`（GPU1／第二張 R9700）。

**音頻**：啟動時先靜音來源 tab（`chrome.tabs.update {muted:true}`，tab id 記在 `chrome.storage`）、mpv 播音（單一播放器，音畫同步最佳）。**暫停來源影片**：啟動時用 `chrome.scripting`（靠 `activeTab`，點圖示即授予）把頁上 `<video>` `pause()`、停止時 `play()` 恢復（省串流/CPU、避免雙重畫面）。**視窗管理**：「停止」鈕 → `{stop:true}` 殺 mpv＋解除 tab 靜音＋恢復影片；新一次啟動會先殺上一支 mpv（replace，mpv PID 持久化在 `~/.config/fgvk/mpv.pid`，含等舊的退出釋放 GPU）。**自動還原（mpv 被自己關掉）**：`background.js`（MV3 service worker）在 `mpv_started` 時開 `connectNative` port（open native port 讓 worker 不被 Chrome 暫停＋host 常駐）；host 的 `monitor_loop` 每 ~1s 查 mpv PID，mpv 運行→退出（使用者手動關窗）時 push `mpv_exited` 回 worker → 解除 tab 靜音＋恢復影片（音頻自動回來，不必非按「停止」）。**錯誤回饋**：啟動後等 3s 確認 mpv 存活（`proc.poll()`，含 zombie 修正）、若已退出（如影片不可用/DRM）讀 log 回傳錯誤；啟動失敗時 popup rollback 還原來源 tab。**profile 自動修復**：啟動前檢查 conf.toml 有標準 4 支 profile、缺了就補（只補缺的、不覆蓋現有的）——防 Lossless Scaling GUI 存檔把 conf.toml 覆蓋掉 profile。後台 tab 不玩（省電）。

**anime1.me 支援**：yt-dlp 解析不了此站，host 的 `resolve_anime1(url)` 走兩段 API＋Cookie——(1) GET 集數頁（`/postid`、`?p=`、`?cat=` 皆可）抓 `<video data-apireq>`；(2) POST `v.anime1.me/api`（`d=apireq`）拿直接 MP4 `src`＋3 支 Cookie（`e`/`p`/`h`，nginx 驗證、~7h 有效）；(3) mpv 帶 `--http-header-fields=Cookie: ...` 播放（**media host 會輪轉** muan/miru/...，用 API 回的 src）。

**hanime1.me 支援**：yt-dlp 無此站 extractor；host 的 `resolve_hanime1(url)` 直接 GET watch 頁（`/watch?v=<id>`）抓 `<source src=...>` 的**最高畫質** MP4（`vdownload.hembed.com/{id}-{quality}p.mp4?secure=...`，1080p/720p/480p）——**不 gate**（plain GET 即可、無 cookie/Referer 需求），故不需 extra_args。其它站仍走 yt-dlp。

## Key files

| 檔案 | 用途 |
|------|------|
| `manifest.json` | MV3 0.5.0；`tabs` + `nativeMessaging` + `storage` + `scripting` 權限＋`host_permissions <all_urls>`（供 worker 之後還原影片）；`background.service_worker`（`background.js`）；`icons` + `action.default_icon`（像素小鴨） |
| `background.js` | MV3 service worker：`mpv_started` 時開 `connectNative` port → host `monitor_loop` 監控 mpv 存活、push `mpv_exited` → 還原 tab（解除靜音＋影片）；open native port 讓 worker 不被暫停 |
| `icons/` | extension 圖示（像素小鴨，16/32/48/128，透明背景） |
| `popup.html` / `popup.js` | 2x/3x/4x/10x +「停止」鈕；啟動時靜音來源 tab＋暫停 `<video>`、停止時解除＋恢復；連 native host；顯示成功/失敗 |
| `host/fgvk-mpv-launch.py` | native messaging host（讀 stdio length-prefixed message → `launch`/`stop`；mpv PID 持久化在 `~/.config/fgvk/mpv.pid`；新啟動 replace 先殺舊的＋等舊的釋放 GPU；**profile 自動修復**＋**啟動存活檢查/錯誤回饋**）。`--selftest` 可離線驗 |
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
| e2e | 重啟 Chrome → youtube.com 任意影片 → 點圖示 → 2x → mpv 視窗插幀 → 點「停止」→ mpv 關閉＋tab 解除靜音；或直接**關 mpv 視窗** → tab 自動解除靜音＋恢復影片（background monitor） |

## 取決 / 前置

- lsfg-vk 2.0.0（`~/.local/share/vulkan/implicit_layer.d/VK_LAYER_LSFGVK_frame_generation.json` 已存在、layer 已 hook）。
- conf.toml 有 4 支 FG profile（見頂層 AGENTS.md 配置表）；若被 Lossless Scaling GUI 覆蓋掉，host 啟動前會**自動補回**缺的 profile。
- GPU 固定 GPU1（兩張 R9700 同 ID，用 PCI BDF 區分）；要換 GPU0 改 host 的 `GPU_SELECT` 常數為 `0000:03:00.0`。

## 陷阱

- extension ID 要靠**金鑰**鎖定（`manifest.json` 的 `key`）才會是固定的 `jggfcgmdjhdeokfjlmnfdmnekodggdjc`；改 key 或移除會讓 ID 變 → NativeMessagingHosts 的 `allowed_origins` 要跟著改。
- native host 要 `chmod +x`，且 `allowed_origins` 的 extension ID 要正確，否則 `connectNative` 回連不上。
- GPU 用 `MESA_VK_DEVICE_SELECT` 固定；兩張 R9700 device id 相同，只用 `1002:7551` 無法區分，要帶 PCI BDF。
- mpv PID 持久化在 `~/.config/fgvk/mpv.pid`；`read_pid()` 殺前會驗證 process 存活**且** `comm==mpv`（防 PID 重用殺錯），所以殘留的舊 PID 無害。
- 存活檢查用 `proc.poll()`（mpv 是 host 的子程序）；**不可用 `os.kill(pid,0)`**——mpv 退出未 reap 時是 zombie，`os.kill` 會誤判為存活。
- 啟動後等 3s 確認 mpv 存活才回傳（錯誤回饋），故 popup 回應慢約 3s；**慢失敗**（網路 timeout 超過 3s 才退出）抓不到、只會回 ok（mpv 稍後自己退出）。
- monitor host（`connectNative` port 模式）只讀 PID_FILE（`read_pid` 驗證、不殺），不與 launch/stop host 衝突；port 斷開時 Chrome 殺 host、`select(stdin)` 收 EOF 退出。worker 在 `mpv_started` 開監控、worker 啟動時若 `mutedTabId` 存在會重開（browser 重啟自愈）。
- mpv 的 `--http-header-fields` 是 String list，**要用 `=` 形式**（`--http-header-fields=Cookie: ...`）；用空格分開（`--http-header-fields "Cookie: ..."`）會報 `option requires parameter`。anime1.me 的 MP4 靠這個 Cookie header 過 nginx（403 門），缺了會 403。
- `hanime1.me` 的域名**包含** `anime1.me` 子字串（`han**ime1.me**`），所以 `main()` 必須**先查 hanime1**（`if "hanime1.me" ... elif "anime1.me"`）；順序反了會讓 hanime1 URL 被 anime1 分支吃掉而解析失敗。
