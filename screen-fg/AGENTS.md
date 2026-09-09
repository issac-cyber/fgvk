# screen-fg

瀏覽器影片即時插幀工具（本機個人用）。依 `../specs/screen-fg-pipeline/spec.md`（LOCKED v1.0）建置。

## Scope

單一 binary `screen-fg`：portal ScreenCast v5 捕捉瀏覽器視窗 → CPU 去重（32×32 MAD）→ SDL3 全螢幕視窗 + Vulkan FIFO swapchain 呈現 → `lsfg-vk 2.0.0` implicit layer 插幀。

## Key files

- `CMakeLists.txt` — 建置（需 `libsdl3-dev libpipewire-0.3-dev libvulkan-dev libglib2.0-dev`；portal 用 GDBus/gio-2.0，非 raw libdbus——本機 libdbus 1.16.2 的 container API 是坏的）
- `src/main.cpp` — 主迴圈（單執行緒 poll：PipeWire / SDL / GDBus）
- `src/capture.{hpp,cpp}` — portal GDBus（`g_bus_get_sync` + `g_dbus_connection_call_sync`）+ PipeWire 捕捉
- `src/frame.hpp` — 中性 `CapturedFrame` 定義 + `kPixFmtBgra`（=44，== `SPA_VIDEO_FORMAT_BGRA`）；**不依賴 PipeWire**（frame 定義與 PW 解耦）
- `src/frame_source.hpp` — `FrameSource` 抽象介面（`Capture` / `SyntheticSource` 共同實作；include `frame.hpp`）
- `src/synthetic.{hpp,cpp}` — 測試用合成幀來源（**不含** PipeWire 標頭，可編進純測試）
- `src/dedup.{hpp,cpp}` — 32×32 luminance grid + MAD 去重
- `src/present.{hpp,cpp}` — SDL3 視窗 + Vulkan（CPU upload + FIFO swapchain，非 dmabuf import）+ HUD
- `src/config.{hpp,cpp}` — 純 `resolveConfig(tomlText, envMap)` + `cardIndexFor(displayIndex)`；`loadConfig` 是讀檔 adapter
- `src/reexec.{hpp,cpp}` — 純 `resolveReexec(state, action, hudOn)` → (target, stateEnv, hudEnv)；`selfPath`/`ensurePlainCopy`/`reexec` 是注入 self-path 的薄 adapter
- `src/clamp.hpp` — 倍數 clamp 計算
- `shared/protocol.hpp`（兄弟目錄 `../shared/`）— stdio 控制協議**單一來源**：`Message`（Status|Exit）、詞彙常數、純 emit/parse + 欄位存在性檢查（缺欄 → nullopt，非默默 default）
- `tests/` — config / config_resolve / dedup / clamp / reexec / frame_source 單元測試（純，**不 link PW/SDL/Vulkan**）

## 操作

- `./build/screen-fg` — 啟動（portal picker 選視窗）
- `./build/screen-fg --passthrough` — 純呈現模式（立即 re-exec `screen-fg-plain` 複製本）
- FG 視窗內鍵盤：Esc 退出、P 暫停/恢復
- 建置：`cmake -B build && cmake --build build`
- 跑純模組測試：`./build/screen-fg-tests`（config / config_resolve / dedup / clamp / reexec / frame_source，68 checks，不 link PW/SDL/Vulkan）
- **依賴 `../shared/`**：`CMakeLists` 以 `-I <fgvk>/` 引用 `shared/protocol.hpp`；該目錄是與 `screen-fg-gui` 共用的純協議模組
- 無門戶驗證（synthetic）：`DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 [DISABLE_LSFGVK=1] SCREENFG_DISPLAY=1 ./build/screen-fg`
  （`DISABLE_LSFGVK=1` 關 layer 只驗呈現管線；不設 = 連 lsfg-vk layer 一起驗。正常應 `結束（N 帧捕捉 / 1 帧呈現）` + exit 0）

## 暫停 / passthrough 機制（Q8a）

layer 是 per-process 全域單例、config 初始化只讀一次 → 無法運行時切換。
暫停 = **re-exec 到 `screen-fg-plain` 的複製本**（`cp` 真檔案，不是 symlink：
`/proc/self/exe` 會被 kernel 解析 symlink，symlink 改變不了 layer 的辨识結果）。
複製本執行檔名不匹配 conf.toml 的 `active_in` → layer 自我 unload →
繼續呈現原始幀、無插幀（spec Q8a 語義）。狀態走 `SCREENFG_STATE=paused/normal` env。
來源視窗關閉（PipeWire stream 進 error）→ 自動退出（spec Q9）。
**注意**：re-exec 重跑 `source->start()` → 真實捕捉模式下 XDG portal picker 會**重跑**（需重新選窗）；synthetic 看不到。HUD 運行期狀態用 `SCREENFG_HUD` env 跨 re-exec 保留（啟動時讀回）。
**re-exec 決策是純函數** `resolveReexec(currentlyPaused, action, optional<hud>)`（`src/reexec.cpp`，可單測）：`action` ∈ {PassthroughBoot, Pause, Resume, Toggle}；hud 用 `std::optional<bool>`（boot 傳 nullopt=不設 HUD env，pause/resume/P 傳值）。self-path 由 adapter 注入。4 個 call site（boot/--passthrough、pause 命令、resume 命令、P 鍵）全走 resolver。

## GDBus / portal 陷阱（本機實測）

- **跑前設乾淨 bus 路徑**：env 的 `DBUS_SESSION_BUS_ADDRESS` 帶過期 guid 會 "Did not receive a reply"。
  用 `DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus"`。
- **CreateSession 務必同時帶 `handle_token` + `session_handle_token`**：portal 1.21.1 缺 session token 會 NoReply/crash。
- **token 只可用 `[a-zA-Z0-9_]`**：request object path 不接受 dash（會 "Invalid token"）。
- **expected reply type 要含外層 tuple**：回 `o` 的方法 → `G_VARIANT_TYPE("(o)")`（不是 `"o"`）。
- **`g_variant_new` 的 `@a{sv}` 會 adopt dict 的 ref（不 inc）**：建完 params 後不要再 `g_variant_unref(dict)`（會 double-free）。
- **抽未知型別 child 用 `g_variant_get_child_value`**（回 GVariant* 要 unref）；`g_variant_get_child` 的 format 不能用 `"^"`。
- **已知非致命 quirk**：`g_dbus_connection_call_sync` 在 GLib 2.88 會打 `g_atomic_ref_count_dec` assertion（GLib-CRITICAL，不 crash、exit 0）——是 GLib build 內部問題，非本 code 的 dict/params 處理（已隔離驗證）。

## Vulkan 呈現陷阱（本機實測，Mesa 26.0.8 radv + SDL3 0.4.2 + Wayland）

- **`VkImageMemoryBarrier.image` 務必設定**：本 code 曾漏設（zero-init → `VK_NULL_HANDLE`），radv 在 record 時 dereference null image → segfault。
  `VkImageSubresourceRange` **沒有** `image` 成員（image 在 barrier 本體上）。staging image 的兩個 barrier（DST→SRC、SRC→DST）都要設 `.image`。
- **swapchain `imageUsage` 要含 `TRANSFER_SRC + TRANSFER_DST`**：blit 寫入 swapchain image 要 `TRANSFER_DST`；lsfg-vk layer 捕捉呈現幀要讀（`TRANSFER_SRC`）。只給 `COLOR_ATTACHMENT` → blit 非法 GPU 操作。
- **本機只有 1 個 Vulkan device**（雖有 2 張 R9700：card0/card1 各驅一塊 3440x1440；Vulkan 只 enumerate 到 dev 0，且 `vkGetPhysicalDeviceSurfaceSupportKHR` 回 support=1）。GPU 選擇無歧義。
- **`vkQueuePresentKHR` 崩根因（已解 2026-09-09）= `VkPresentInfoKHR.pSwapchains` 漏設**：設 `swapchainCount=1` 但 `pSwapchains` 是 nullptr → 無效 present，radv 崩在 WSI per-image loop。本 code 的 minimal 與 full present 都漏了。補上 `pi.pSwapchains = &im.swap` 即解。
  - 排查紅鵲（全排除）：buffer 數量、surface mapping/extent（windowed 恆 0xFFFFFFFF 但 vkcube 亦 unmapped 仍正常）、color space、size、minImageCount、llvmpipe（vkcube 在 lvp 正常→非 driver bug）、display index、layer、windowed/fullscreen、present mode。
  - 崩點取證：gdb batch + 崩位址反組譯 = count/array loop（`images[r]` deref）。
- **`vkGetSwapchainImagesKHR` 務必呼叫兩次（fill 版）**：先 `(..., &nImg, nullptr)` 取 count，再 `(..., &nImg, imgs.data())` 填 handle。本 code 曾於 cleanup 時**丟失 fill 呼叫** → `swapImages[i]` 停在 `VK_NULL_HANDLE` → 之後 `vkCreateImageView` deref null 崩。診斷手法：寫最小 SDL+Vulkan 程序（window→surface→device→swapchain→imageview）對照，可快速區分「環境壞」vs「code 壞」（最小程序通過即 code 問題）。
- **Vulkan semaphore 是 one-shot**：`vkAcquireNextImageKHR` signal 的 `acquireSem` 一旦被 submit `wait`（消耗）過，**不可再被 present `wait`**（會永久阻塞、連 SIGTERM 都殺不掉，因卡在做 blocking 的 Vulkan call）。正確流：submit wait acquireSem + signal readySem → present 只 wait readySem。
- **`shutdown()` 要 idempotent**：`~Presenter()` 和 main 都會叫 `shutdown()` → destroy 後要把 `im.dev/inst/window` 設回 null，否則二次 destroy 已死 device → `vkDeviceWaitIdle: Invalid device`。
- **本機 Vulkan header 是裁剪版**，幾個簽名與官方不同：`vkResetCommandBuffer(cmd, flags)`（**無 device 參**）；`VkColorSpaceKHR` 是 enum；`vkCmdCopyBufferToImage` 第 4 參是 `dstImageLayout`；`VK_QUEUE_PRESENT_BIT` 未 declare（用 0x2）；`VkSubmitInfo` 無 `pFences`；`VkImageSubresourceRange` 無 `image` 成員。
