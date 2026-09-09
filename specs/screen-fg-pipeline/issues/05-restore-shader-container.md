# 05 Task: 恢復 `lsfg-vk.dll` shader container（硬前置）

Type: task
Status: resolved
Blocked by:

## Question

本機缺少 `lsfg-vk.dll`（FG model 的 shader container，PE 檔含 SPIR-V blobs）——layer 載入、profile 匹配後，在 swapchain init 時硬失敗（"The specified shader DLL does not exist"）；vkcube 與 `lsfg-vk-cli benchmark` 路徑現況全斷。2.0.0 與 dev26 的 tarball 都不含此檔，需要外部取得。

要做的事：

1. **查明來源**：`lsfg-vk.dll` 從哪裡來？（Steam 版 Lossless Scaling 的安裝內容？decky-lsfg-vk plugin 的安裝流程？lsfg-vk docs 的 Lossless Scaling 安裝頁？查 lsfg-vk.dev docs 與 decky-lsfg-vk README。）本機的 Steam `Lossless Scaling` 資料夾是 .NET 9 win-x64 的 `LosslessScaling.dll` app——確認它與 shader container 的關係（`fixDllPath()` 把 app 資料夾映射到 `lsfg-vk.dll`）。
2. **取得並放置**：把 `lsfg-vk.dll` 放到 `~/.local/share/Steam/steamapps/common/Lossless Scaling/`（conf.toml 的 `dll` key 指向的資料夾）。
3. **驗證**：vkcube（或任何 Vulkan app + 匹配 profile）不再出現 "shader DLL does not exist"；`(lsfg-vk) [INFO]: Loaded lsfg-vk layer version 2.0.0` 後不再硬失敗；可選：vLLM 空閒時跑 `lsfg-vk-cli benchmark` 確認 model 能載入。

HITL：使用者需確認/提供 shader container 的取得途徑（涉及 Lossless Scaling 授權）；agent 負責查證與驗證。

## Comments

### 2026-09-08 研究（agent）

**本機現況**：
- Lossless Scaling（Steam 993090）**已安裝**（appmanifest：StateFlags=4, 183MB, LastPlayed ≈2026-07-16）。
- 安裝的 app 版本 = **2.0.240405.15**（2024-04-05 build，**stable 分支**）。
- Steam 的 `Lossless.dll`（7.5MB）**有** PE `.rsrc` 段（7.1MB），含 **98 個 SPIR-V magic**，但**沒有** `'mipmaps'`/`'generate_8bit'`/`'generate_16bit'` 字串（ASCII 與 UTF-16 皆無）。
- 試過 `cp Lossless.dll lsfg-vk.dll`：layer 過關「shader DLL does not exist」，但隨後硬失敗 `Unable to find base shader 'mipmaps' in DLL` → 版本不符（app 2024-04 vs layer 2.0.0 / 2026-09）。
- 官方 2.0.0 tarball（builds.lsfg-vk.dev）**不含** `lsfg-vk.dll`（已列目錄證實：只有 cli/ui/layer/manifest）。
- 免費移植 `lossless-scaling-lsfg/LosslessScaling` 是 **stub**（27 個檔案全 <400 bytes 的骨架，無 shader）。
- git.lsfg-vk.dev 只有 `master` 與 `mangohud-integration` 分支；Codeberg 是 placeholder。
- SourceForge 鏡像有 anti-bot 擋住（未取到檔案清單）。

**解法（2.0.0 release 公告原文）**：
> "Please don't forget to switch to the 'lsfg-vk' branch of Lossless Scaling on Steam."

即：**Steam 的 Lossless Scaling app 有 `lsfg-vk` update channel/branch**，其 build 的 shader container 才與 2.0.0 layer 相容（2024-04 的 stable build 不相容）。changelog 亦："enhancement: `Lossless.dll` is automatically corrected to `lsfg-vk.dll` in the configuration file."

**剩餘步驟（HITL — Steam GUI，30 秒）**：
1. Steam →  libraries → 右鍵 `Lossless Scaling` → `Properties` → `Updates` → `Update Channel` 選 **`lsfg-vk`**（若下拉看不到該 branch，選 Custom 填 `lsfg-vk`）。
2. Steam 更新 app 後，**刪除** agent 先前 `cp` 出的舊 `lsfg-vk.dll`（它不是 depot 檔案，不會被自動更新覆蓋）。
3. Agent 驗證：`vkcube` 應出現 `(lsfg-vk) [INFO]: Loaded lsfg-vk layer version 2.0.0` 且 swapchain init 不再硬失敗（預期能看到 FG 參數生效、vkcube 正常跑）；可選：vLLM 空閒時 `lsfg-vk-cli benchmark`。

### 附：相關 project（給 spec/建置參考）
- **Luka0611/Lsfg-Android-Application-Poco-x8** — 本 effort 架構的 Android 先例：user-supplied `Lossless.dll` → on-device 抽出 SPIR-V → `MediaProjection` 截螢幕 → 跑 LS FG → 合成到 system overlay。
- **xXJSONDeruloXx/decky-lsfg-vk** — Decky plugin：自動裝 layer + 自動偵測 Steam LS DLL + 全套 UI（FPS multiplier / flow scale / perf mode / launch option 工具）。
- **xodiosx/lsfg-vk-arm64** / **Vogelhaufen/lsfg-vk-20250809** — lsfg-vk 跨平台 build 的 fork。
- 主 repo：**git.lsfg-vk.dev/lsfg-vk**（master）。授權 CC BY-NC-ND 4.0（本機個人用 OK）。

### 2026-09-09 執行與驗證（agent）

使用者在 Steam GUI 完成 channel 切換。驗證結果：

- Steam 更新完成：buildid 19655272 → **24979361**，資料夾由 183MB（.NET runtime）變為 9.6MB——**`lsfg-vk` branch 的 build 直接 shipped `lsfg-vk.dll`（2.1MB，container 正確大小）**，agent 先前 `cp` 出的舊檔已被 depot 更新取代；`README.txt` 指向 lsfg-vk.dev。
- `lsfg-vk-cli healthcheck`：**"Healthcheck did not find any issues with the lsfg-vk installation"**（cli 2.0.0 / layer 2.0.0）。
- `vkcube` 15s 全程：**layer 載入 → profile 匹配（4x FG / 85% [Performance]）→ instance init（half precision）→ device init，零 shader 錯誤**（2026-09-08 的 `Unable to find base shader 'mipmaps'` 不再出現）。
- `DISABLE_LSFGVK=1 vkcube`：零 lsfg-vk log lines——off switch 正常。
- benchmark 跳過：兩張 R9700 被 vLLM 佔用（剩 0.7GB / 2.5GB VRAM），std::bad_alloc 風險；非票面必要驗證。

**Answer：`lsfg-vk.dll` 來源 = Steam Lossless Scaling app 的 `lsfg-vk` update channel（官方 app 作者開的分支；branch build 直接附帶與 2.0.0 layer 相容的 2.1MB container，README 指向 lsfg-vk.dev）。放置 = app 資料夾（conf.toml `dll` key 所指），由 Steam depot 自動管理（不需手動 cp）。授權：shader 來自使用者已購買的 Steam app；layer 授權 CC BY-NC-ND 4.0。**
