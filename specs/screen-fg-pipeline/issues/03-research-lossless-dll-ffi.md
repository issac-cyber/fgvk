# 03 Research: Lossless.dll FFI（架構 B fallback 可行性）

Type: research
Status: resolved
Blocked by:

## Question

Fallback 架構 B（直接驅動 FG model、繞過 implicit layer）：`lsfg-vk` layer 是怎麼在 Linux 上呼叫 **.NET 9 win-x64** 的 `Lossless.dll` 的？

1. **機制**：lsfg-vk 是 embed/host .NET runtime，還是有 native FFI shim？入口 symbol / entry point 是什麼（model load、給 2 張輸入幀做 inference、回傳生成幀）？
2. **Source 查證**：lsfg-vk 專案已搬離 GitHub（docs 在 lsfg-vk.dev）——查它的 source 現在在哪、layer 呼叫 Lossless.dll 的代碼路徑。
3. **Precedent**：`LSFG-Android`（"frame generation on Android via the lsfg-vk pipeline"，GitHub: FrankBarretta/LSFG-Android）如何在 implicit layer 之外驅動 pipeline？它用哪條路徑（layer 的 pipeline 函式 vs 直接 dotnet host）？
4. **結論**：一個 standalone 工具（不載 layer）能不能直接驅動 FG model（2 輸入幀 → 1 生成幀）？要哪些依賴（dotnet runtime 版本、.so vs .dll、NV 上 FP16 加速的預設）？

Findings 寫進本檔 `## Comments`，再按 tracker 規則 resolve。

## Comments (2026-09-08 research)

### 本地二進位取證（~/.local/lib/liblsfg-vk-layer.so，v2，2026-09-06 安裝）

- `nm -D`：動態依賴只有 glibc / cxxabi / libstdc++ / `dlopen`+`dlsym` / `inotify`+`poll`（config watch）/ pthread。**零** `hostfxr`/`hostpolicy`/`coreclr`/dotnet 符號或字串（grep 計數 = 0）。
- `strings`：`libvulkan.so` / `libvulkan.so.1` + `Function pointer to vkGetInstanceProcAddr is null` → `dlopen/dlsym` 是用來載入 **Vulkan loader**（`vk::detail::DispatchLoaderDynamic`，volk 式動態載入），不是載 .NET。
- 字串 `Lossless.dll` / `LosslessScaling.dll` / `Lossless Scaling` / `SteamAppId` / `wine` / `proton` / `/proc/self/maps` / `/proc/self/comm` / 5 種 Steam 安裝路徑 = **程式識別 + shader 檔搜尋**（`utils::identifyProcess` + `utils::findDll`，見 source），不是呼叫 .NET。
- `Loading shader library from ...` / `The specified shader DLL does not exist` = `ShaderLibrary`（解析 shader 容器）。
- .NET 9 win-x64 的 `Lossless.dll`（Steam 版，7.5MB）是 .NET assembly，PE export table 只有 11 個 Windows app API（`Activate`/`ApplySettings`/`GetAdapterNames`/`GetDisplayNames`/`GetDwmRefreshRate`/`GetForegroundWindowEx`/`Init`/`IsWindowsBuildAtLeast`/`SetDriverSettings`/`SetWindowsSettings`/`UnInit`）——**沒有任何 FG inference 入口**。層層從未把它當 code 執行。
- 本機 `lsfg-vk.dll` **不在** Steam app 資料夾（全磁碟搜尋無果）；`~/.config/lsfg-vk/conf.toml` 的 `dll = "<app folder>"` 會解析到不存在的 `.../Lossless Scaling/lsfg-vk.dll`。

### 機制（Q1）：不是 dotnet host、不是 FFI 進 .NET——是 PE 資源抽取器

Source（git.lsfg-vk.dev/lsfg-vk v2.0.0）：

- `lsfg-vk-pipeline/src/library/dll.cpp` — `lsfgvk::parseDll(path)`：`std::ifstream` 讀整檔 → DOS(0x5A4D) → PE("PE\0\0"，必須 PE32+) → optional header 的 resource table → section headers 定位 `.rsrc` → **RT_RCDATA (id=10)** → language directory → data entries → 回傳 `map<resourceID, bytes>`。**純檔案解析：不 dlopen、不載入 .NET assembly、不呼叫任何 .NET 函式。**
- `library.cpp` — `ShaderLibrary(dld, device, halfPrecision, dll, logCb)`：base shaders `mipmaps`(rid 2147488584) / `generate_8bit`(rid 1) / `generate_16bit`(rid 2) + 24 個 pass（alpha0-3, beta0-4, gamma0-4, delta0-4, epsilon0-4）；**quality = rid、performance = rid+24、FP16 = rid+48**（`halfPrecision ? idx+48 : idx`）。每個 blob → `vkCreateShaderModule`（SPIR-V）。
- 1.0.0（github.com/Vogelhaufen/lsfg-vk-20250809，搬離前 snapshot）：thirdparty = `pe-parse` + `dxbc` + `volk` + `toml11`——同樣 PE 資源解析 + DXBC 解碼，`framegen/public/lsfg_3_1.hpp` 的 `initialize(...)` 帶一個 `loader` 回呼（按名字載 shader source）。
- `config.cpp` — shader 檔解析：`conf.toml [global] dll` → `fixDllPath()`：指到 `Lossless Scaling` 資料夾 / `Lossless.dll` / `LosslessScaling.dll` 一律 → **同目錄的 `lsfg-vk.dll`**；`LSFGVK_DLL_PATH` 環境變數可覆寫；`findDll()` 掃 5 種 Steam 安裝路徑（`~/.local/share/Steam/steamapps/common/Lossless Scaling/lsfg-vk.dll` 等）+ 當前目錄。
- **`lsfg-vk` pipeline 全程純 compute shader**（v2 `pipeline.cpp`：bindless pipeline、greedy-fit memory、persistent cache 在 `~/.cache/lsfg-vk/cache_{quality|performance}_<uuid>.bin`）；flow 估計也是 DLL 裡的 compute shader（signature 的 flow image，extent × flowScale）。**不用 VK_NV_optical_flow**（v2 source 全部 grep = 0 參照；.so 裡的 NV 拓展 entry-point 字串是 volk/DispatchLoader 動態表，非 pipeline 使用）。

**v2 公開 API**（`lsfg-vk-pipeline/include/lsfg-vk/lsfgvk.hpp`，CC BY-NC-ND 4.0）：

```cpp
lsfgvk::Instance(deviceId /*名稱 / "10de:2c02" / PCI bus id*/, shaderDllPath,
                 allowFP16, leakInstance=false, logCallback=nullptr);
lsfgvk::Context(inst, width, height, flowScale, performanceMode);
FileDescriptors fds = ctx.exportFds();
// sourceFd: 2 層 RGBA8 image（2 個輸入 slot，frame N 寫 image (iteration % 2)）
// destinationFd: 1 層 RGBA8（生成幀寫入處）
// syncFd: timeline semaphore（signal so → lsfg-vk signal so+1）
ctx.dispatch(total, unsignaled);  // pre-pass
ctx.acquire(unsignaled);          // 每個生成幀的 main-pass
ctx.idle();                       // 等最後一幀
```

`Instance` 內部自建 Vulkan **1.2** instance/device（`vkhelper.cpp`）：拓展 = `VK_KHR_synchronization2` + `VK_KHR_external_memory_fd` + `VK_KHR_external_semaphore_fd`（VK_OPAKE_FD）+ timeline semaphore（1.2 core）+ `shaderFloat16` feature（`checkHalfPrecisionSupport` 只查這個 feature bit）；compute-only queue family 優先；GPU 識別支援名稱/vid:did/PCI bus id。

**1.0.0 公開 API**（`framegen/public/lsfg_3_1.hpp`，同有 `lsfg_3_1p.hpp` performance 版）：

```cpp
LSFG_3_1::initialize(deviceUUID, isHdr, flowScale, generationCount, shaderLoader /*回呼*/);
LSFG_3_1::createContext(in0 /*fd*/, in1 /*fd*/, outN /*fds*/, extent, format);
LSFG_3_1::presentContext(id, inSem, outSem);
LSFG_3_1::deleteContext(id);
LSFG_3_1::finalize();
```

**層（v2 `wrapper.cpp`）的 data path = standalone tool 的藍圖**：
1. lazy init：`setenv(DISABLE_LSFGVK)` → `fixDllPath/f findDll` → `new Instance(deviceId, dllPath, allowHalfPrecision, leakInstance=true)`
2. `Context` → `exportFds()` → 匯入 game device（OPAQUE_FD）：syncFd→timeline semaphore、sourceFd→2 層 image、destinationFd→1 層 image
3. `dispatch()`：blit game swapchain 幀→source layer `iteration % 2` → signal timeline → `ctx.dispatch(n)`
4. `acquire()`：(wait timeline) blit destination→swapchain → `ctx.acquire()`；最後幀→fence

### Source 位置（Q2）

- **官方 repo：git.lsfg-vk.dev/lsfg-vk**（作者 PancakeTAS；2026-08-27 宣布搬離 GitHub 並清空重建；tag 2.0.0 / 2.0.0-rc1；v2 完整重寫；autobuild 在 builds.lsfg-vk.dev）。
- **授權：CC BY-NC-ND 4.0**（2026-08-27 起，從 GPLv3 改；動機 = AI slop forks）：**禁止商業使用與衍生作品**；要 maintain/fork 需直接聯絡作者。
- Codeberg mirror（codeberg.org/PancakeTAS/lsfg-vk）：blog 內已被刪除線；目前只有 README.txt placeholder（milestone 頁仍在）。
- v1 archive：git.lsfg-vk.dev/lsfg-vk-archive；搬離前 snapshot：github.com/Vogelhaufen/lsfg-vk-20250809。
- 呼叫路徑（v2）：`lsfg-vk-layer/src/wrapper.cpp`（DLL 解析 + Instance/Context + FD import + blit）→ `lsfg-vk-pipeline/src/lsfgvk.cpp`（Instance/Context 實作）→ `library.cpp`（ShaderLibrary）+ `library/dll.cpp`（parseDll）+ `pipelines/v3_1.cpp`（pipeline signature）+ `pipeline.cpp`（bindless pipeline 建構）；config 在 `lsfg-vk-config/src/config.cpp`。
- 附帶：`mangohud-integration` 分支（2026-09-07 draft）正在為 MangoHud 整合寫 `api.h` 公開 C API。

### Precedent：LSFG-Android（Q3）

github.com/FrankBarretta/LSFG-Android（709 stars，"frame generation on Android via the lsfg-vk pipeline"）：

- **不走 implicit layer**（Android 12+ 禁止把外部 code 載進 non-debuggable 進程）、**也不走 dotnet host**：submodule `lsfg-vk-android` 是 **lsfg-vk 1.0.0 的 branch + `#ifdef __ANDROID__` patch set**（AHardwareBuffer image sharing、`createContextFromAHB`、`waitIdle`；Linux build path 不變），app 用 CMake `add_subdirectory()` 直接吃 `framegen/`。
- Data path：`MediaProjection` capture → 幀經 **AHardwareBuffer** 共享給 framegen 內部 device（`VK_ANDROID_external_memory_android_hardware_buffer`）→ `LSFG_3_1::createContextFromAHB(...)`（Linux 上即 FD-based `createContext(in0, in1, outN, extent, format)`）→ `presentContext`（semaphore 同步）→ 生成幀合成到 system overlay。比 Linux layer 多 50–80ms 延遲。
- **Shader 條款（README 原文）**："You need a legitimately purchased copy of Lossless Scaling. The `Lossless.dll` is **not** shipped, downloaded, or bundled by anything in this repository. The user picks their own DLL via the Storage Access Framework, the app extracts the shaders on-device into its private storage, then deletes the DLL copy."——即 shader 是**從使用者提供的 DLL 抽出來**（v1.0.0 用 pe-parse/dxbc；v2 用 parseDll），DLL 本身從不被 load 成 code。
- **結論：precedent 路徑 = 直接 build 並呼叫 framegen 庫的公開 API（與 layer 同一份 code），不是 dotnet host、也不是隱式 layer 鉤子。**

### 結論（Q4）

**Standalone tool（不載 layer）可以直接驅動 FG model：2 輸入幀 → 1 生成幀，可行。**

- **不需要 .NET runtime**：零 dotnet 依賴；"DLL" 只是 PE 資源容器，幾十行純 C++ 解析即可（parseDll）。
- 依賴清單：
  1. **`lsfg-vk.dll` shader 容器**——必須從正版 Windows Lossless Scaling 取得（lsfg-vk 官方不 bundle）。⚠️ 本機 Steam app 資料夾目前**沒有**這個檔（conf.toml 指到不存在的解析路徑，layer 現狀應該在 "The specified shader DLL does not exist" 狀態）；standalone tool 一樣需要，可用 `LSFGVK_DLL_PATH` / conf `dll` key 指路徑。
  2. **Vulkan 1.2 + 拓展**：`VK_KHR_synchronization2`、`VK_KHR_external_memory_fd`、`VK_KHR_external_semaphore_fd`（VK_OPAKE_FD）、timeline semaphore（1.2 core）；FP16 時需 `shaderFloat16` feature（可選）。**不需要 VK_NV_optical_flow**（純 compute shader，任何 Vulkan GPU 皆可——v2 把 Vulkan 降到 1.2 正為支持所有 GPU）。
  3. **lsfg-vk source**（CC BY-NC-ND 4.0：個人/非商用 OK；商用與 derivative 需與作者另議）——從 source 編 `lsfg-vk-pipeline` + `lsfg-vk-helper`（+ `lsfg-vk-utils`）成 static lib（官方 prebuilt tarball 只有 layer/ui/cli，**沒有 framegen library**，本機 `linux/lsfg-vk-2.0.0-dev26-linux.tar.xz` 同此）。
  4. **NV 上 FP16**：非自動預設——`allowFP16` 是 flag，gate 在 device 的 `shaderFloat16` feature（v2 code 無 "2:1 ratio" 偵測）。v2 default config `allowHalfPrecision = true`（本機 conf 亦 `allow_fp16 = true`）；`LSFGVK_NO_FP16=1` 強制關。實務：2:1 FP16 ratio 硬體（AMD laptop/handheld）2-3x 加速無品質損失；NV desktop（1:1）效益有限但無害；shader 選 rid+48 的 FP16 變體。
- 最小 standalone 序列（照 wrapper.cpp + FileDescriptors 協議）：
  1. `Instance("10de:xxxx", "<path>/lsfg-vk.dll", true)`
  2. `Context(inst, W, H, 1.0f, false)` → `exportFds()`
  3. 在自己 device（同 GPU，OPAQUE_FD）匯入：sourceFd→2 層 RGBA8、syncFd→timeline semaphore、destinationFd→1 層 image
  4. 每幀：blit 輸入幀 A→source layer 0、輸入幀 B→source layer 1 → signal timeline(so) → `ctx.dispatch(1)` → `ctx.acquire()` → wait timeline(so+1) → 從 destination 讀生成幀 → `ctx.idle()`

### 關鍵提醒

- 真正的 shader 容器檔名叫 **`lsfg-vk.dll`**（Windows app 隨附）；`Lossless.dll`/`LosslessScaling.dll` 在 `fixDllPath` 裡只是「同目錄」線索。
- 授權 CC BY-NC-ND 4.0：個人使用 OK，任何產品化/derivative 需作者同意。
- v2.0.0 於 2026-09-05 發布；本機 .so 為同期建置（2026-09-06 安裝）。

## Answer

1. **機制**：都不是。layer **沒有 embed/host .NET runtime，也沒有 native FFI shim 呼叫 .NET**——`.so` 零 hostfxr/hostpolicy/coreclr 符號，`dlopen/dlsym` 只用於 `libvulkan.so`（Vulkan loader）。真正的機制：layer（與 framegen 庫）把 shader 容器 PE 檔**當純資料讀**——`parseDll()`（`library/dll.cpp`）解析 PE32+ 的 `.rsrc`/RT_RCDATA、按 resource ID 抽出 raw SPIR-V blobs（base：mipmaps/generate_8bit/generate_16bit；24 個 pass，quality=rid、performance=rid+24、FP16=rid+48）→ `vkCreateShaderModule`。入口（v2 公開 API `lsfgvk.hpp`）：model load = `lsfgvk::Instance(deviceId, shaderDllPath, allowFP16, ...)` 內建 `ShaderLibrary`；2 輸入幀→生成幀 = `Context(inst, w, h, flowScale, perfMode)` → `exportFds()`（sourceFd 2 層輸入 / destinationFd 輸出 / syncFd timeline semaphore，OPAQUE_FD 匯入自己的 device）→ `dispatch(total)` → 每幀 `acquire()` → `idle()`。
2. **Source**：**git.lsfg-vk.dev/lsfg-vk**（作者 PancakeTAS，2026-08-27 搬離 GitHub、清空重建 v2；tag 2.0.0；Codeberg mirror 已形同廢棄——只剩 placeholder；v1 archive 在同機 `lsfg-vk-archive`，搬離前 snapshot 在 github.com/Vogelhaufen/lsfg-vk-20250809）。授權改為 **CC BY-NC-ND 4.0**（非商用、禁 derivative）。layer 呼叫 "DLL" 的代碼路徑：`lsfg-vk-layer/src/wrapper.cpp`（DLL 解析 + FD import + blit）→ `lsfg-vk-pipeline/src/lsfgvk.cpp` → `library.cpp`（ShaderLibrary）+ `library/dll.cpp`（parseDll，即「呼叫」Lossless 的全部）。
3. **LSFG-Android**：不走 implicit layer（Android 12+ 禁止外部 code 注入）、**不走 dotnet host**——它把 lsfg-vk 1.0.0 的 **`framegen/` 庫直接 CMake `add_subdirectory()` 編進 app**，呼叫同一份公開 API（`LSFG_3_1::initialize/createContext(FD)/presentContext/finalize`；Android 加 `createContextFromAHB`/`waitIdle`），幀經 AHardwareBuffer/FD 共享給 framegen 內部 device，shader 由使用者正版 DLL 在 device 上抽取。**與 layer 同一條路徑：framegen 庫公開 API，非 dotnet。**
4. **結論：可行。** standalone tool 可直接驅動 FG model（2 輸入幀→1 生成幀），**不需要 .NET runtime**。依賴：① `lsfg-vk.dll` shader 容器（須從正版 Windows Lossless Scaling 取得；⚠️ 本機 app 資料夾目前缺此檔，conf 指到不存在路徑）；② Vulkan 1.2 + `VK_KHR_synchronization2`/`external_memory_fd`/`external_semaphore_fd`（OPAQUE_FD）+ timeline semaphore（**不需要 VK_NV_optical_flow**，純 compute shader，NV 任意近代 driver 即可）；③ 從 source 編 `lsfg-vk-pipeline`(+helper)（CC BY-NC-ND 4.0：非商用、禁 derivative）；NV FP16 非自動——`allowFP16` flag gate 在 `shaderFloat16` feature，NV desktop（1:1）效益有限、AMD iGPU/handheld（2:1）2-3x 加速。
