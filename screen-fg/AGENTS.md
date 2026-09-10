# screen-fg

瀏覽器影片即時插幀工具（本機個人用）。依 `../specs/screen-fg-pipeline/spec.md`（LOCKED v1.0，2026-09-09；§13 為建置偏離附錄）建置。

## Scope

單一 binary `screen-fg`：portal ScreenCast v5 捕捉瀏覽器視窗 → CPU 去重（32×32 MAD）→ SDL3 全螢幕視窗 + Vulkan FIFO swapchain 呈現 → `lsfg-vk 2.0.0` implicit layer 插幀。另提供 **stdio 控制通道**（GUI 用它；CLI 也可用：stdin 命令 / stdout status JSON / stderr log）。音頻路徑不動（瀏覽器自己播、本工具靜音）。

## Key files

- `CMakeLists.txt` — 建置（需 `libsdl3-dev libpipewire-0.3-dev libvulkan-dev libglib2.0-dev`；portal 用 GDBus/gio-2.0，非 raw libdbus——本機 libdbus 1.16.2 的 container API 是坏的）；兩個 target：`screen-fg` + 純 `screen-fg-tests`（ctest 目標名 `unit`）
- `src/main.cpp` — 啟動流程（config → 來源 → 呈現 → clamp + 健康檢查）＋主迴圈（單執行緒 poll：PipeWire / SDL / GDBus / **stdin 控制命令**）＋protocol transport（`protocol::emit*` 發 status/exit JSON 到 stdout、log 全走 stderr、`writeAll` 處理 EINTR/EPIPE）＋HUD 文字
- `src/capture.{hpp,cpp}` — portal GDBus（`g_bus_get_sync` + `g_dbus_connection_call_sync`）+ PipeWire 捕捉；`start()` **阻塞到使用者選完視窗**；PipeWire 主迴圈跑在獨立交替線程、`poll()` 保持 DBus 連線健康；`isDead()` = 來源視窗關閉 / 串流斷掉（Q9 自動退出）
- `src/frame.hpp` — 中性 `CapturedFrame` 定義 + `kPixFmtBgra`（=44，== `SPA_VIDEO_FORMAT_BGRA`）；**不依賴 PipeWire**（frame 定義與 PW 解耦）
- `src/frame_source.hpp` — `FrameSource` 抽象介面（start/poll/nextFrame/isDead/stop；`Capture` / `SyntheticSource` 共同實作，main 迴圈不 care 來源是哪種）
- `src/synthetic.{hpp,cpp}` — 測試用合成幀來源（16×16 移動方塊 + 漸變；raw 每幀不同、但方塊太小動不了 32×32 MAD 超過預設閾值 3.0 → dedup 只放行第一幀（第一幀永遠 true）→ 仍觸發完整呈現管線 + layer；跑完 N 帧後 `isDead()` 回 true → 乾淨退出；**不含** PipeWire 標頭，可編進純測試）
- `src/dedup.{hpp,cpp}` — 32×32 luminance grid + MAD 去重（輸入 raw BGRA w×h×4；**第一幀永遠 true**；`droppedCount()`）
- `src/present.{hpp,cpp}` — SDL3 borderless 全螢幕視窗 + Vulkan（CPU 上傳 BGRA → staging → blit scale → 選配 HUD → FIFO present；**非 dmabuf import**）+ HUD（資源於 init 常備、執行中 `setHud` 切換即生效）
- `src/config.{hpp,cpp}` — 純 `parseConfig`（flat-TOML 子集）/ `resolveConfig(tomlText, envMap)`（env 層覆蓋 toml 層）/ `cardIndexFor`（本機 identity：display N 在 card N）；`loadConfig` 是讀檔 + 真實 env 收集的 adapter
- `src/reexec.{hpp,cpp}` — 純 `resolveReexec(currentlyPaused, action, optional<hud>)` → `ReexecPlan`（targetName/stateEnv/hudEnv）；`selfPath`/`ensurePlainCopy`/`reexec` 是注入 self-path 的薄 adapter
- `src/clamp.hpp` — `maxMultiplier(displayHz, contentFps)` = hz÷fps（0 回 1）；`maxMultiplierForCapture(hz)` = hz÷60（spec Q4：window 捕捉上限 60fps）
- `shared/protocol.hpp`（兄弟目錄 `../shared/`）— stdio 控制協議**單一來源**：`Message`（Status|Exit）、詞彙常數（State/Cmd）、`kVersion`、純 emit/parse + 欄位存在性檢查（缺欄 → nullopt，非默默 default）
- `tests/` — 6 個純單元測試檔（config / config_resolve / dedup / clamp / reexec / frame_source），74 checks，**不 link PW/SDL/Vulkan**

## 操作

- `./build/screen-fg` — 啟動（portal picker 選視窗；`start()` 阻塞到選完）
- `./build/screen-fg --passthrough` — 純呈現模式（立即 re-exec `screen-fg-plain` 複製本、無插幀；boot 不設 HUD env）
- `./build/screen-fg --config <path>` — 選配 config 路徑（預設 `~/.config/screen-fg/config.toml`）
- FG 視窗內鍵盤：Esc 退出、P 暫停/恢復
- 建置：`cmake -B build && cmake --build build`
- 跑純模組測試：`./build/screen-fg-tests`（74 checks）或 `ctest -R unit`
- **依賴 `../shared/`**：`CMakeLists` 以 `-I <fgvk>/` 引用 `shared/protocol.hpp`；該目錄是與 `screen-fg-gui` 共用的純協議模組
- 無門戶驗證（synthetic）：`DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 [DISABLE_LSFGVK=1] SCREENFG_DISPLAY=1 ./build/screen-fg`
  （`DISABLE_LSFGVK=1` 關 layer 只驗呈現管線；不設 = 連 lsfg-vk layer 一起驗。正常應 `結束（3 帧捕捉 / 1 帧呈現）` + exit 0——見上 synthetic 條目說明）

## Config & env

config.toml 是**flat-TOML 子集**（每行 `key = value`、`#` 註解、unknown key 忽略、壞值 throw 帶行號；缺檔案 = 純預設）：

| key | 型別 | 預設 | 說明 |
|------|------|------|------|
| `dedup_threshold` | float | `3.0` | MAD 閾值（0..255 尺度） |
| `hud` | true/false | `true` | HUD 開關（env 可覆蓋、stdin `hud 0|1` 可運行中改） |
| `profile` | quoted string | `"2x FG / 100%"` | lsfg-vk profile name |
| `gpu` | quoted string | `""`（auto） | PCI bus ID override（如 `"0000:01:00.0"`）；不設 = 跟 display 的 card |
| `display` | int | `0` | FG 視窗蓋的螢幕 index（GPU 跟該 display 走；`cardIndexFor` 本機 identity） |
| `content_fps_cap` | uint | `60` | 內容幀率上限（<60 時倍數再依它 clamp + 警告） |
| `capture_mode` | `window`\|`monitor` | `window` | 捕捉來源：`window` = spec 原行為（portal picker 選視窗）；`monitor` = 全螢幕 monitor 捕捉（`types=1`（MONITOR）、無 picker、tolerate 空 parent）。**本機 picker 不顯示、故本機 config 用 `monitor`**（詳 spec §13） |

resolve 優先序：**defaults → config.toml → env**（env 覆蓋 toml）：
- `SCREENFG_HUD="1"/"0"` → 覆蓋 `hud`
- `SCREENFG_DISPLAY=N` → 覆蓋 `display`
- `SCREENFG_STATE="paused"` → 啟動即 paused（passthrough）模式
全部由純函數 `resolveConfig` 一次 resolve（不直接碰 getenv、可測）；`loadConfig` 是讀檔 + 收真實 env 的 adapter。

## 啟動流程（main.cpp 順序）

1. `ensurePlainCopy`（**每次都重新** cp `screen-fg-plain`——build 更新後舊 copy 會過期、曾導致 pause re-exec 跑到舊 binary）→ 解析 `--config` → `loadConfig`（toml + env resolve）
2. 有 `--passthrough` → 立即 re-exec 成 plain 複製本
3. frame 來源：`SCREENFG_SYNTHETIC` 非 `"0"` → `SyntheticSource`（1280×720、N 帧）；否則 `Capture`（portal CreateSession → SelectSources picker → Start → OpenPipeWireRemote，`start()` 阻塞到選完；失敗 → 發 exit JSON + return 1）
4. `Presenter.init`（SDL3 borderless 全螢幕 + Vulkan FIFO swapchain + GPU 選擇；失敗 → stop 來源 + exit JSON + return 1）
5. clamp（maxMult = floor(hz/60)；`content_fps_cap < 60` → 再 clamp + 警告）＋啟動健康檢查（**不靜默降級**、明確警告）：
   - `vkEnumerateInstanceLayerProperties` 找 `VK_LAYER_LSFGVK_frame_generation`（找不到 → 警告、檢查 implicit_layer.d）
   - `~/.config/lsfg-vk/conf.toml` 有 `active_in` 含 `"screen-fg"` 的 profile（沒有 → 警告：layer 會自我 unload、FG 不會啟動）
   - 讀 `/sys/class/drm/cardN/device/mem_info_vram_total|used`：free VRAM < 2048 MB → 警告（可能有其他程序佔 GPU）

## stdio 控制通道

- **stdout 只有協議 JSON 行**：初始 status（0, maxMult, layer, state）→ 主迴圈 **1Hz 心跳**（與是否收到幀無關、讓 GUI 穩定更新）→ `exiting` status → `{"type":"exit","code":N}`。log 全走 **stderr**。
- **stdin 行式命令**（name 小寫比較；非阻擋 `pollStdinCmd`：poll fd 0 + 累積完整行 + 去尾 `\r`）：
  - `quit` → 乾淨退出
  - `pause` / `resume` → re-exec（只在狀態真的切換時才 re-exec、同狀態 no-op）
  - `hud 1` / `hud 0` → `presenter.setHud` 立即生效（不重啟）
- `writeAll` 處理 EINTR（continue）/ EPIPE（停止寫、GUI 已斷、不崩）

## 主迴圈（每 iteration 順序）

1. 1Hz status 心跳（emitStatus）
2. poll stdin 命令（見上）
3. `SDL_WaitEventTimeout(200ms)`：`SDL_EVENT_QUIT` / `SDLK_ESCAPE` → 退出；`SDLK_P` → Toggle re-exec
4. `source->poll()`（保持 DBus 健康；PipeWire 在獨立交替線程）→ `isDead()`（synthetic 跑完 / 來源視窗關閉，Q9）→ 退出
5. `nextFrame()`（nullopt → continue）→ fps 計算（1 秒滑窗）→ `frameCount++` → `dedup.isDifferent` → 不同則 `presentCount++` + 更新 HUD 文字 + `presentFrame`
6. 退出清理：`exiting` status → stderr `結束（N 帧捕捉 / M 帧呈現）` → `source->stop()` → `presenter.shutdown()` → exit JSON → return 0

## 暫停 / passthrough 機制（Q8a）

layer 是 per-process 全域單例、config 初始化只讀一次 → 無法運行時切換。
暫停 = **re-exec 到 `screen-fg-plain` 的複製本**（`cp` 真檔案，不是 symlink：
`/proc/self/exe` 會被 kernel 解析 symlink，symlink 改變不了 layer 的辨识結果）。
複製本執行檔名不匹配 conf.toml 的 `active_in` → layer 自我 unload →
繼續呈現原始幀、無插幀（spec Q8a 語義）。狀態走 `SCREENFG_STATE=paused/normal` env。
來源視窗關閉（PipeWire stream 進 error）→ 自動退出（spec Q9）。
**注意**：re-exec 重跑 `source->start()` → 真實捕捉模式下 XDG portal picker 會**重跑**（需重新選窗）；synthetic 看不到。HUD 運行期狀態用 `SCREENFG_HUD` env 跨 re-exec 保留（啟動時讀回）。
**re-exec 決策是純函數** `resolveReexec(currentlyPaused, action, optional<hud>)`（`src/reexec.cpp`，可單測）：`action` ∈ {PassthroughBoot, Pause, Resume, Toggle}；hud 用 `std::optional<bool>`（boot 傳 nullopt=不設 HUD env，pause/resume/P 傳值）。self-path 由 adapter 注入。4 個 call site（boot/--passthrough、pause 命令、resume 命令、P 鍵）全走 resolver。
**詞彙注意**：`ReexecEnv`（"normal"/"paused"）是 env 詞彙；status JSON 用 "running"/"paused"/"exiting"——兩套不同詞彙、不可混用（reexec.hpp 有明確註解）。

## 測試

6 個測試檔、74 checks（純，**不 link PW/SDL/Vulkan**）：
- `test_config` — parseConfig：defaults / float / bool / quoted / 壞值 throw（帶行號）
- `test_config_resolve` — resolveConfig：env 覆蓋（HUD/DISPLAY）、STATE→paused、`cardIndexFor`
- `test_dedup` — 不同/相同幀判定、droppedCount、小尺寸幀
- `test_clamp` — maxMultiplier / maxMultiplierForCapture：167→2、144→2、120→2、119→1、60→1、240→4、144/30→4、0→1
- `test_reexec` — resolveReexec：4 action × paused 狀態 × hud optional（targetName/stateEnv/hudEnv）
- `test_frame_source` — SyntheticSource：frame 有效、pixFmt==BGRA、size/stride

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
