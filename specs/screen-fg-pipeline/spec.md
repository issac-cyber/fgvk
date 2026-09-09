# Spec: Screen → FG → Fullscreen（瀏覽器影片的即時插幀）

> **狀態：LOCKED v1.0（2026-09-09）**
> 機器：Ubuntu 26.04 / GNOME Wayland / 2× AMD R9700（各帶一屏）/ lsfg-vk 2.0.0 已裝 + shader 已恢復
> 建置是**另外一個 effort**；本 spec 只鎖定設計。

## 1. 目的與範圍

本機個人工具 `screen-fg`：把**瀏覽器視窗**的內容（24/30/60fps 影片）高率捕捉、去重、經 `lsfg-vk 2.0.0` implicit layer 做 frame generation，輸出到**同螢幕**的 borderless 全螢幕視窗。音頻不動（照舊從瀏覽器出）。

不做：音頻 delay 對齊、多螢幕通用化、整屏捕捉（回饋迴路）、分發、>60Hz 捕捉、B 套後端的首版實作。

## 2. 系統總覽

單一 binary、單一 process：

```
[瀏覽器視窗] --(xdg-desktop-portal ScreenCast v5 + PipeWire)--> 原始 BGRA buffer
     |
     v
[dedup] CPU 降採樣 32×32 luminance grid + MAD 閾值，只留「與上一幀不同」的幀
     |
     v
[screen-fg present app] C++ / SDL3 視窗（borderless 全螢幕）+ Vulkan 1.2 FIFO swapchain
     |  (dmabuf zero-copy import → staging copy → swapchain image)
     v
[FG backend — 可插拔]  A: lsfg-vk implicit layer hook（預設）
                      B: lsfgvk public API direct（文件化 fallback，首版不實作）
     |
     v
[compositor 以 ~2× 速率顯示]（同螢幕）
```

Audio 路徑不變：瀏覽器自己播，FG 視窗靜音。

## 3. 捕捉（依 01 research）

- **路徑**：xdg-desktop-portal **ScreenCast v5**（DBus）拿 session/stream 權限 + **PipeWire client API** 過 `OpenPipeWireRemote` fd 收 per-frame buffer。GNOME Wayland 下唯一路徑（mutter 把一切擋在 portal 後）。
- **目標**：**視窗** target（啟動時走 portal picker 選定，每次啟動重選——不 cache 授予）→ 消滅「抓到 FG 自己輸出視窗」的回饋迴路。
- **幀率**：window stream event-driven、**上限 60fps**（90fps 做不到）→ 已定案：**只做 window target / 60fps cap，monitor capture + crop 選項砍掉**（2× 60 = 120 ≤ 167Hz 螢幕放得下；>60fps 內容 2× 也放不開，無價值）。
- **已知坑（01）**：portal 1.21.1 crash bug——`CreateSession` 不給 `session_handle_token` 會把整個 portal 打死（務必生 token）；bus name 已遷到 `org.freedesktop.portal.Desktop`。
- **buffer 特性**：1:1 紋理讀取（無 scaling/dithering）、游標隱藏、格式與螢幕相同（BGRA）。

## 4. 去重（dedup）

- **方案（定案）**：CPU 把 frame **降採樣成 32×32 luminance grid**，與上一幀算 **mean absolute difference（MAD）**，`MAD < 閾值` → 視為重複、丟掉；否則送 present。
- **閾值**：預設 **3/255**，config 可調。對編碼壓縮噪聲穩健；1080p 約 1–2ms；零 GPU 佔用。
- **不採用**：全 buffer 嚴格相等（壓縮噪聲下必失效）；全 buffer hash + 閾值（8MB 全讀太慢太脆）；GPU compute pass（多一段同步沒必要）。
- 結果：VFR 安全（只呈現不同幀），24fps 影片 → 每秒 ~24 次 present。

## 5. present app（Vulkan）

- **stack（定案）**：C++ 直接上正式版——**SDL3**（視窗 + Wayland WSI）+ **Vulkan 1.2 loader** + **PipeWire C API** + glib。無 Python 原型階段（三個硬骨頭全是 C 側事）。
- **buffer 流程**：PipeWire buffer 以 **dmabuf zero-copy import** 進 Vulkan（`VK_KHR_external_memory_fd` + `DMA_BUF` fd handle，read-only）→ copy 到 staging image → swapchain image → present。（layer 只 hook present，import 路徑不涉層。）
- **視窗**：SDL3 **borderless 全螢幕**（同螢幕、覆蓋整屏）；swapchain **`VK_PRESENT_MODE_FIFO`**（layer 會强制，VRR 要關——02 結論）。
- **倍數 clamp（定案）**：啟動時 `max_mult = floor(display_hz / 60)`（167Hz → 60fps 內容最多 2×）；profile 倍數超了 → **自動 clamp 到放得下的最大值 + 明確 warning**（4× 用在 24fps 內容 = 96fps ✓ 照常）。
- **延遲預算（02 實測）**：2× FG 端到端 ≈ **17–35ms**（FG compute ≲1–2ms + FIFO 排隊 1–2 VBlank）→ 落在「接受 ~2 幀音畫偏移」的決定內。
- **FG 參數**：走 lsfg-vk 既有 profile 機制（`active_in` 匹配 `screen-fg` binary name）；影片預設 **2×**（既有 profile "2x FG / 100%"）。

## 6. Shader container（ticket 05 已解決）

- **來源（定案）**：Steam Lossless Scaling 的 **`lsfg-vk` update channel**（官方 app 作者的分支）——branch build 直接 shipped 相容的 2.1MB `lsfg-vk.dll`，由 Steam depot 自動管理（buildid 24979361），不需手動 cp。
- 2.0.0 期望的 container：PE 檔 `.rsrc` 內**數字 resource ID 1–26**（`mipmaps` 特殊 ID 2147488584）對應 v3.1 shader 集合（來源：`lsfg-vk-pipeline/src/library.cpp`）。
- 已驗證：`lsfg-vk-cli healthcheck` 零問題、`vkcube` 15s 零 shader 錯誤、`DISABLE_LSFGVK=1` off switch 正常。
- 授權：shader 來自使用者已購買的 Steam app；lsfg-vk 授權 CC BY-NC-ND 4.0（本機個人用 OK、不可分發/改作）。

## 7. FG backend：A 優先，B 文件化 fallback

**整合方式（定案）**：單一 binary、**FG 階段可插拔**；A（implicit layer hook）**預設**；B（direct pipeline）**首版不實作**，spec 寫清 API 與切換條件。

- **A（預設）**：`screen-fg` 用 Vulkan FIFO swapchain present 去重後幀流；lsfg-vk implicit layer hook 插生成幀。啟動時健康檢查：layer 未載入 / profile 未匹配 → 明確訊息（不靜默降級）。
- **B（fallback，未實作）**：v2 public API（`lsfg-vk-pipeline/include/lsfg-vk/lsfgvk.hpp`）：
  ```cpp
  lsfgvk::Instance inst(deviceId /* "0000:01:00.0" */, shaderDllPath, allowFP16);
  lsfgvk::Context ctx(inst, w, h, flowScale, performanceMode);
  auto fds = ctx.exportFds();  // {sourceFd, destinationFd, syncFd} — OPAQUE_FD import
  // sourceFd: 2D RGBA8 array 長度 2（frame A 放 [0]、frame B 放 [1]）
  // destinationFd: 單張 RGBA8 生成幀
  // syncFd: timeline semaphore（signal so 啟動 → signal so+1；讀完再 signal 下一幀）
  ctx.dispatch(total); ctx.acquire(); /* 讀 destination */ ctx.idle();
  ```
  依賴：Vulkan 1.2 + `synchronization2` + `external_memory_fd` + `external_semaphore_fd`（OPAQUE_FD）；**不需要** NV_optical_flow（純 compute）；不需 layer / .NET runtime。
- **切換條件**：profile 匹配失敗 / 某些視窗模式 hook 不到 / 需要更低延遲或更細粒度控制 / 便攜性。

## 8. UX（定案）

- **無 global hotkey**（Wayland 無可靠路徑）。FG 全螢幕視窗自己攬鍵盤：**Esc = 退出、P = 暫停/恢復**。
- **暫停語義（Q8a）**：P = **停止插幀、繼續呈現原始幀**（還看得到瀏覽器在動，只是沒插幀）；再按恢復。
- **視窗選擇**：每次啟動走 **portal picker 對話框**（GNOME 標準，2 秒選一下）；不 cache 授予（避免失效 session 的坑）。
- **視窗被關（Q9a）**：stream 結束 → **工具自動退出** + 打一行提示。
- **FG 視窗生命週期**：隨 tool 生滅。
- **HUD（可關）**：小顯示目前 FPS / multiplier / 已丟重複幀數。

## 9. GPU 選擇（定案）

- **預設 = 跟著「被捕捉視窗所在 output」走**（window → output → DRM card 查得出）：視窗在哪塊屏，就用哪張卡、在那塊屏出 FG。本機雙屏雙 R9700 各帶一屏。
- **Override**：config 用 **PCI bus ID**（如 `0000:01:00.0`）強制指定。

## 10. 配置與建置

- **Tool 名**：`screen-fg`；專案位置 `projects/screen-fg/`（建置 effort 時建立）。
- **Config**：`~/.config/screen-fg/config.toml`——dedup 閾值（預設 3/255）、HUD 開關（預設 on）、強制選卡（預設跟視窗）、選哪個 lsfg-vk profile（預設 "2x FG / 100%"）。首次運行全預設。
- **建置**：CMake；依賴 `libsdl3-dev`、`libpipewire-0.3-dev`、`libvulkan-dev`（+ vulkan headers）、`libglib2.0-dev`。
- **前置**：Steam Lossless Scaling 保持在 `lsfg-vk` update channel（§6）。

## 11. 風險

| 風險 | 緩解 |
|---|---|
| portal 1.21.1 crash bug | 永遠帶 `session_handle_token`；portal 升級跟蹤 |
| 去重閾值過高/過低（誤丟幀/誤送重複） | 32×32 MAD + 可調閾值；24fps 視頻先驗證 |
| 雙屏/視窗跨屏 | 跟視窗 output 走（§9）；override 逃生門 |
| vLLM 佔著 VRAM（剩 0.7/2.5GB） | FG 2× 緩衝約數百 MB；啟動時檢查 free VRAM 不足 → 明確訊息（非 OOM crash） |
| 60fps 內容 4× 超顯示能力 | §5 自動 clamp + warning |
| 視窗移動時 portal stream 跟隨 | portal 視窗 target 自動跟隨（01 已確認） |

## 12. 決策記錄（grilling 2026-09-09，全同意）

| # | 決定 | 結果 |
|---|---|---|
| Q1 | >60Hz 捕捉選項 | **砍掉**（window target / 60fps cap） |
| Q2 | 去重方案 | **CPU 32×32 luminance grid + MAD 閾值（3/255 預設）** |
| Q3 | A/B 整合 | **單一 binary、插拔 backend、A 預設、B 首版不實作** |
| Q4 | 超顯示能力 | **自動 clamp 到 floor(167/60) + warning** |
| Q5 | UX | **無 global hotkey；FG 視窗內 Esc/P；每次 portal picker；HUD 可關** |
| Q6 | 語言/stack | **C++ + SDL3 + Vulkan 1.2 + PipeWire C API + glib，直接上** |
| Q7 | GPU 選擇 | **跟捕捉視窗的 output 走；config PCI bus ID override** |
| Q8 | 暫停語義 | **停止插幀、繼續呈現原始幀** |
| Q9 | 視窗關閉 | **工具自動退出 + 提示** |

## 13. 實作偏離附錄（build effort, 2026-09-09）

- **Q8 暫停實現**：`lsfg-vk` layer 是 per-process 全域單例、config 初始化只讀一次（`hooks.cpp` `Layer::Layer()`）→ **無法運行時切換**。改為：**P 暫停 = re-exec 到 `screen-fg-plain` 的複製本**（`cp` 真檔案，非 symlink——`/proc/self/exe` 會被 kernel 解析 symlink）。複製本執行檔名不匹配 `active_in` → layer 自我 unload → 繼續呈現原始幀、無插幀（保留 Q8 語義；代價：視窗重建 + 重走 portal picker）。
- **Q7 GPU 選擇**：portal 不回報視窗所在 output → 以 config `display` index 指定 FG 視窗所蓋的螢幕（GPU 跟該 display 走），PCI bus ID override 保留。
- **conf.toml**：已加 `[[profile]] active_in = [ "screen-fg" ]`（2x/100%，備份 `conf.toml.bak-screenfg`）。
