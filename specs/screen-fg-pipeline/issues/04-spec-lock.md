# 04 Spec lock（與使用者 grilling 確認每一節）

Type: grilling
Status: resolved
Blocked by: 01, 02, 05

## Comments

**2026-09-08 prep**：spec 草稿已寫於 `.scratch/screen-fg-pipeline/spec.md`（DRAFT）——11 節：§1 目的/§2 總覽圖/§3 捕捉（01 結論）/§4 去重/§5 present app（02 結論）/§6 shader container 要求（含 2.0.0 源碼 `library.cpp` 的 resource ID 表：數字 ID 1–26、mipmap=2147488584）/§7 架構 B（v2 public API 全文語義：`Instance/Context/exportFds→OPAQUE_FD import/dispatch/acquire/idle`、timeline semaphore 協議）/§8 UX/§9 依賴/§10 風險表/§11 七個待 grilling 定案。grilling 時逐節確認 + 解決 §11 即可收 spec。另：v2 public API 頭已 clone 至 /tmp/opencode/lsfg-vk-src（可引用源碼細節）。

## Question

把 charting 決策（見 map 的 Decisions so far）+ research findings（01、02、03）整合成 `.scratch/screen-fg-pipeline/spec.md` 並鎖定：

- 捕捉管線（API 選擇、~90fps、window target）
- 去重機制（fingerprint、容錯、VFR 邊界）
- Present app（Vulkan swapchain、present mode、profile 啟動方式）
- 輸出視窗 UX（喚出/關閉、同螢幕、視窗選擇）
- A/V offset 與 FG 參數預設（2×、flow_scale、performance mode、pacing）
- A→B fallback 路徑（若 03 已 resolve 則寫進 spec；未 resolve 則標記為 open risk）
- 風險與未解事項

HITL：逐節 grill 使用者確認，不許靜默假設。

## Answer

**2026-09-09 grilling 完成**（兩輪：Q1–Q7 + Q8–Q9，使用者全同意建議答案）。Spec 已鎖定於 `.scratch/screen-fg-pipeline/spec.md`（LOCKED v1.0，12 節）。

9 項定案：(1) 砍掉 >60Hz 捕捉選項，window target / 60fps cap；(2) 去重 = CPU 32×32 luminance grid + MAD 閾值（預設 3/255 可調）；(3) 單一 binary、FG 階段可插拔，A（layer hook）預設、B（direct public API）首版不實作但 spec 寫清 API 與切換條件；(4) 超顯示能力自動 clamp 到 floor(167/60) + warning；(5) UX：無 global hotkey、FG 視窗內 Esc 退出 / P 暫停、每次啟動走 portal picker 選視窗、HUD（FPS/倍數/去重數）可關；(6) stack = C++ + SDL3 + Vulkan 1.2 + PipeWire C API + glib，直接上正式版（無 Python 原型）；(7) GPU 跟被捕捉視窗的 output 走、config PCI bus ID override；(8) 暫停 = 停止插幀但繼續呈現原始幀；(9) 視窗被關 → 工具自動退出 + 提示。

另寫入技術預設：buffer 走 dmabuf zero-copy import（VK_KHR_external_memory_fd）；config `~/.config/screen-fg/config.toml`（閾值/HUD/強制選卡/profile，首次全預設）；tool 名 `screen-fg`、專案位置 `projects/screen-fg/`、CMake 建置。建置是另外一個 effort。
