# Map: Screen → FG → Fullscreen（瀏覽器影片的即時插幀）

## Destination

一份鎖定的 spec（`.scratch/screen-fg-pipeline/spec.md`）：這台機器上的工具，把**瀏覽器視窗**高率捕捉、去重，透過一個小型 Vulkan app 呈現，由已裝好的 `lsfg-vk 2.0.0` implicit layer 做 frame generation，輸出到**同螢幕**的 borderless 全螢幕視窗——即「影片 FG」，讓瀏覽器/非遊戲內容也能插幀。**只要 spec**：建置是另外一個 effort。

## Notes

- 機器：Ubuntu 26.04、GNOME **Wayland**（xdg-desktop-portal + PipeWire 可用）、2× AMD R9700（Navi 48, 34.2GB）。
- `lsfg-vk 2.0.0` 已安裝驗證：`~/.local`（bin/lib + implicit layer manifest）、config 在 `~/.config/lsfg-vk/conf.toml`（validate 通過）。
- Steam 的 `Lossless Scaling` 現已切到 **`lsfg-vk` update channel**（buildid 24979361，資料夾 9.6MB）——branch build **直接附帶** `lsfg-vk.dll`（2.1MB shader container，PE 檔含 SPIR-V blobs，layer 從 `.rsrc` 抽出），conf 的 `dll` key 指向該資料夾（03 已釐清，layer 從不呼叫 .NET assembly）。
- 架構 A（layer hook）為主；架構 B（直接驅動 FG model、繞過 layer）是文件化 fallback。
- Research tickets 用 `research` skill（web 研究走 firecrawl CLI）。
- 本 workspace 無 git repo：research findings 寫進 ticket 的 `## Comments` / `## Answer`；map 由主 session 集中更新（避免併發衝突）。

## Decisions so far

- **目的地與範圍**：本機個人工具；effort 以鎖定 spec 結束（建置另開 effort）。
- **架構**：A 優先（小型 capture→present Vulkan app 被 lsfg-vk 2.0.0 implicit layer hook），B（direct pipeline）備選。
- **捕捉目標**：捕捉**瀏覽器視窗**（portal window target，非整屏）——消滅「抓到 FG 自己的輸出視窗」的回饋迴路。
- **幀率策略**：高率捕捉 + 自動去重連續重複幀；只呈現不同幀（24/30 原生速率、VFR 安全）。*修訂（依 `01-research-screen-capture`）*：portal window stream 是 event-driven 且上限 60fps——60fps 捕捉 + 去重已涵蓋 24/30/60 內容；>60Hz 需 monitor capture + client-side crop（選擇留在 spec lock）。
- **輸出 UX**：同螢幕的 borderless 全螢幕視窗（覆蓋螢幕）；source app 繼續播放。
- **音頻**：不改變——聲音照舊從 source app 即時出；輸出視窗靜音。
- **音畫同步**：接受約 2 幀偏移（FG 固有代價，不 delay 音頻）。*修訂（依 `02-research-layer-hook-feasibility`）*：2× FG 端到端 ≈ 17–35ms（FG compute ≲1–2ms + FIFO 排隊 1–2 VBlank），落在 2 幀預算內。
- **FG 參數**：經 lsfg-vk 既有 profile 機制由使用者設定；影片預設 2×。
- [01 Research: GNOME Wayland 程式化螢幕捕捉](issues/01-research-screen-capture.md)：portal ScreenCast v5 + PipeWire client API 是唯一路徑（mutter 把一切擋在 portal 後面）；frame 是 1:1 紋理讀取（無 scaling/dithering、游標隱藏）→ 去重 hash 原始 buffer 即可。
- [02 Research: lsfg-vk layer hook requirements](issues/02-research-layer-hook-feasibility.md)：`override_present_mode` 預設強制 FIFO（app 用 `VK_PRESENT_MODE_FIFO` 即可，VRR 要關）；啟動用 `active_in` binary 匹配（或 `LSFGVK_PROFILE`）；layer 無內建去重——app 自己負責餵「每幀都不同」且保持穩定節拍。
- [03 Research: Lossless.dll FFI](issues/03-research-lossless-dll-ffi.md)：premise 修訂——layer 從不呼叫 .NET assembly；shader container（**`lsfg-vk.dll`**，PE 檔含 SPIR-V blobs，從 `.rsrc` 抽出）才是 model 載體；架構 B 經 v2 public API 可行（無 dotnet 需求）；source 搬到 git.lsfg-vk.dev/lsfg-vk，授權 **CC BY-NC-ND 4.0**（本機個人用 OK、不可分發/改作）。
- [05 Task: 恢復 shader container](issues/05-restore-shader-container.md)：**已解決（2026-09-09）**——Steam Lossless Scaling 切 `lsfg-vk` update channel（官方 app 作者的分支），branch build 直接 shipped 相容的 2.1MB `lsfg-vk.dll`（Steam depot 自動管理，buildid 24979361）；`lsfg-vk-cli healthcheck` 零問題、`vkcube` 15s 零 shader 錯誤、off switch 正常。Shader 授權：使用者已購買的 Steam app。
- [04 Spec lock](issues/04-spec-lock.md)：**鎖定（2026-09-09）**——兩輪 grilling（Q1–Q9）全定案，spec 於 `spec.md`。要點：砍 >60Hz 選項；去重 = CPU 32×32 grid + MAD 閾值；單一 binary 插拔 backend（A 預設、B 首版不實作）；超顯示能力自動 clamp；無 global hotkey、FG 視窗內 Esc/P、每次 portal picker、HUD 可關；C++ + SDL3 + Vulkan 1.2 + PipeWire C API 直接上；GPU 跟視窗 output 走；暫停 = 停插幀續呈現原始幀；視窗被關 → 自動退出。

## Not yet specified

- （空）——2026-09-09 全部定案，spec 已鎖定（見 Decisions 的 04 行 + `spec.md` §12 決策記錄）。

## Out of scope

- 音頻重路由 / delay 音頻對齊影片——已決定不做（「音頻不改變」）。
- 通用化支援（其他 distro / compositor / 多螢幕架構）——只做這台機器。
- 整屏捕捉——已被瀏覽器視窗捕捉取代（回饋迴路）。
