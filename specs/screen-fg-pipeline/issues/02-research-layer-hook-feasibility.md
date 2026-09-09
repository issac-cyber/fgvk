# 02 Research: lsfg-vk layer hook 對 capture→present app 的 requirements

Type: research
Status: resolved
Blocked by:

## Question

架構 A：一個小型新 Vulkan app 持續以自備 swapchain 呈現去重後的幀；已裝好的 `lsfg-vk 2.0.0` implicit layer 要 hook 它做 FG。釘死 app 要做什麼、layer 才會動：

1. **Present mode**：哪些 present mode 可行（VK_PRESENT_MODE_FIFO vs immediate）？layer 是否需要 vsync/pacing？（查 lsfg-vk docs：pacing modes、wiki Quirks 的 VSync 條目、`override_present_mode`）
2. **Profile 啟動**：怎麼保證 layer 對我們的 app 生效——`LSFGVK_PROFILE` 環境變數 vs config 的 `active_in` binary 匹配？2.0.0 的環境變數名（LSFGVK_PROFILE / LSFGVK_CONFIG 等）。
3. **30fps 輸入流的行為**：2× FG → 60Hz 輸出；R9700 上 performance mode 的典型端到端延遲（ms 或幀數）？
4. **Self-disable 偵測**：layer 無匹配 profile 會 self-disable（已知 cosmetic issue #482 的 loader 訊息）；怎麼確認 layer 真的載入並生效（lsfg-vk 的 log 行、LSFGVK_LOG_LEVEL / LSFGVK_LOG_FILE）？
5. **Precedent 確認**：本機的 vkcube + 2.0.0（config 有 `active_in = [ "vkcube", "vkcubepp" ]` 的 4x profile）是已知工作的路徑；一個「capture→present」的 app 與 vkcube 在 layer 眼裡有什麼不同（present 模式/swapchain 配置）可能導致差異？

Findings 寫進本檔 `## Comments`，再按 tracker 規則 resolve。

## Comments

Research date: 2026-09-08. Sources: lsfg-vk docs (https://lsfg-vk.dev/docs/...), GitHub wiki, v2.0.0 release blog, discussion #474, plus direct inspection of the local v2.0.0 install (manifest JSON, conf.toml, `lsfg-vk-cli`, `strings` on `liblsfg-vk-layer.so`) and live test runs on this machine (R9700 / radv / Wayland).

### Local machine facts (verified 2026-09-08)

- Implicit layer manifest: `~/.local/share/vulkan/implicit_layer.d/VkLayer_LSFGVK_frame_generation.json` — name `VK_LAYER_LSFGVK_frame_generation`, type GLOBAL, `implementation_version: 2`, `api_version: 1.4.350`, `disable_environment: {"DISABLE_LSFGVK":"1"}`.
- Config `~/.config/lsfg-vk/conf.toml`: `[global] allow_fp16=true, dll="/home/issac/.local/share/Steam/steamapps/common/Lossless Scaling"` (a **directory**, not a file); profile 1 `4x FG / 85% [Performance]` (`active_in=["vkcube","vkcubepp"]`, `multiplier=4`, `flow_scale=0.85`, `performance_mode=true`, `pacing="none"`); profile 2 `2x FG / 100%` (no `active_in`, `multiplier=2`, `flow_scale=1.0`, `performance_mode=false`, `pacing="none"`).
- `lsfg-vk-cli validate`: "The configuration file is valid." `validate -p` prints both profiles as "Pacing mode: Vsync" (see Q1 note on `none`).
- `lsfg-vk-cli healthcheck`: passes; CLI 2.0.0 + layer 2.0.0.
- **CRITICAL: the shader DLL is missing.** There is no `lsfg-vk.dll` anywhere on this filesystem (checked `/home/issac`, `/opt`, `/usr/local`, `/usr/share`, and a full `/` search). The Steam "Lossless Scaling" dir contains only the Windows `Lossless.dll` (PE32+), and its `linux/` subfolder only holds `lsfg-vk-2.0.0-dev26-linux.tar.xz` (contains cli/ui/layer.so/manifest — **no dll**).
- **The "known working" vkcube path is currently BROKEN on this machine** (contradicts the ticket premise):
  ```
  $ vkcube
  (lsfg-vk) [INFO]: Loaded lsfg-vk layer version 2.0.0
  (lsfg-vk) [INFO]: Using profile with name '4x FG / 85% [Performance]' (identified via executable)
  ...
  (lsfg-vk) [INFO]: Initializing lsfg-vk instance with half precision enabled
  (lsfg-vk) [ERROR]: An error occured while initializing the lsfg-vk swapchain:
  (lsfg-vk) [ERROR]: - The specified shader DLL does not exist
  vkcube: ./cube/cube.c:1533: demo_prepare_swapchain: Assertion `!err' failed.
  ```
  The layer loads, matches the profile, hooks instance/device, then fails at swapchain init and **the app crashes** (the error propagates out of `vkCreateSwapchainKHR`). `DISABLE_LSFGVK=1 vkcube` runs clean (no layer lines, no error) — the app itself is fine. Consequence for our app: a broken/missing DLL = our swapchain creation fails too; and `lsfg-vk-cli benchmark` also fails (`std::bad_alloc`, consistent with the missing shader DLL) — no local R9700 benchmark numbers could be produced today.
- Process identification strings in `liblsfg-vk-layer.so`: `/proc/self/exe`, `/proc/self/comm`, `/proc/self/maps` (binary-name / process-name / Wine-exe matching).
- Pacing validation in the binary: `Invalid pacing mode:` / `Unknown pacing mode`; the only accepted pacing values are **`vsync`** and **`none`**.
- Env vars present in the binary: `LSFGVK_CONFIG`, `LSFGVK_PROFILE`, `LSFGVK_OVERRIDE_PRESENT_MODE`, `LSFGVK_PACING_MODE` (plus `DISABLE_LSFGVK` from the manifest; `LSFGVK_LOG_LEVEL` / `LSFGVK_LOG_FILE` per docs).
- Non-matching app test: `vulkaninfo --summary` (matches no profile; its own X11 surface failure is unrelated) prints **zero `(lsfg-vk)` log lines** and only:
  ```
  ERROR: [Loader Message] Code 0 : loader_create_instance_chain: Failed to find 'vkGetInstanceProcAddr' in layer "/home/issac/.local/share/vulkan/implicit_layer.d/../../../lib/liblsfg-vk-layer.so"
  ```
  This is the "cosmetic loader message" the ticket attributes to issue #482. Note: the lsfg-vk repo has **GitHub Issues disabled** (API: 410 "Issues are disabled for this repo"), so #482 cannot be resolved; the nearest match is **discussion #474** (Help, Feb 2026, https://github.com/PancakeTAS/lsfg-vk/discussions/474), whose log shows this exact message alongside a working profile match — i.e. the message is not, by itself, proof of self-disable.
- `LSFGVK_PROFILE` env-var test: `LSFGVK_PROFILE="2x FG / 100%" vkcube` → `(lsfg-vk) [INFO]: Using profile with name '2x FG / 100%' (identified via environment)` — **it works in 2.0.0 without `LSFGVK_ENV=1`** (confirmed the v2.0.0-rc1 changelog enhancement: "Global properties can now be overriden with environment variables without requiring LSFGVK_ENV=1").

### Q1 — Present mode / pacing

- Docs (pacing-modes page, https://lsfg-vk.dev/docs/configuration/pacing-modes/): the only *documented* pacing mode is `vsync` = no explicit frame pacing; frames are presented as soon as ready, but **Vsync must be enabled** (the layer force-enables it) "otherwise frames will be skipped". Background text explains the mechanism: FIFO forces the Vulkan queue to accept one image per VBlank.
- `override_present_mode` (config-options page, https://lsfg-vk.dev/docs/configuration/configuration-options/): default `true` — "lsfg-vk will override the present mode to Vsync/FIFO, as required for frame pacing to function correctly. Do not turn this option off, unless you know what you are doing." Env: `LSFGVK_OVERRIDE_PRESENT_MODE=0` disables.
- Wiki (https://github.com/PancakeTAS/lsfg-vk/wiki/Injecting-frames-into-Vulkan-apps, "Synchronizing frame insertions"): the insertion/synchronization solution is "simply turning on the FIFO present mode … takes a submitted frame each VBlank", and the layer does this **at swapchain creation**: "we switch to the FIFO present mode, as well as increase the amount of frames rendered by one plus the intermediate images." So: the app's requested present mode is rewritten by the layer (with the default on); `VK_PRESENT_MODE_IMMEDIATE` is not something the app needs to (or should) request — with override on, FIFO wins. Immediate mode would break the one-image-per-VBlank pacing (frames would be presented/dropped unsynchronized).
- Quirks page (https://github.com/PancakeTAS/lsfg-vk/wiki/Quirks): "VSync: Explicitly enable VSync in the game's graphics settings and avoid overriding the present mode using lsfg-vk" (i.e. don't fight the layer); "VRR: Disable Variable Refresh Rate as it is currently not supported"; disable other Vulkan layers.
- Local config `pacing="none"`: valid in the 2.0.0 binary (accepted values are exactly `vsync`/`none`), but `lsfg-vk-cli validate -p` reports it as "Pacing mode: Vsync" — i.e. `none` (no layer-side pacing) + default `override_present_mode=true` still ends up FIFO-paced, and the CLI reports the effective mode. The docs page is stale (still says "no other pacing modes"). Practical takeaway: either value is fine as long as `override_present_mode` stays at its default `true` (FIFO is forced); do not set `preserve_swapchain_image_count` to true unless a crash forces it (docs warn it "may cause stuttering and slowdown").
- Conclusion for our app: present with `VK_PRESENT_MODE_FIFO` (or any mode — the layer rewrites it to FIFO by default). 30fps input → 2x at 60Hz maps 1 real + 1 generated = 2 frames per 33.3 ms ≈ exactly 2 VBlanks at 60Hz; the input cadence must be stable (Vsync pacing only consumes one image per VBlank, so input jitter beyond ~1 VBlank yields dropped/duplicated output frames). Fixed 60Hz monitor, VRR off.

### Q2 — Profile activation

- Two mechanisms, both verified in 2.0.0:
  1. `active_in` (recommended by docs, https://lsfg-vk.dev/docs/getting-started/): matches Linux binary names (last part of the path), Windows exes, process names, or Steam App IDs. Binary strings confirm matching against `/proc/self/exe` (binary), `/proc/self/comm` (process name), `/proc/self/maps`. Hot-reloadable (docs: multiplier/flow/perf hot-reload; profile config via UI hot-reloads).
  2. `LSFGVK_PROFILE='<profile name>'` env var — "overrides automatic profile detection" (env-vars page). Empirically verified: `(identified via environment)`. In 2.0.0 global property env vars (incl. `LSFGVK_PROFILE`, `LSFGVK_CONFIG`, `LSFGVK_LOG_*`, `LSFGVK_MULTIPLIER` etc.) work **without** `LSFGVK_ENV=1` (rc1 changelog); the old v1 `LSFG_*` names (`LSFG_CONFIG`, `LSFG_PROCESS`, `LSFG_BENCHMARK=...`) are from the v1 wiki era (https://github.com/PancakeTAS/lsfg-vk/wiki/Configuring-lsfg%E2%80%90vk) and are not the 2.0.0 interface.
- A profile with **no `active_in` (like our `2x FG / 100%` profile) is NOT auto-activated** — verified: `vulkaninfo` (no matching binary) produced no `(lsfg-vk)` lines, i.e. the layer self-disabled.
- `LSFGVK_CONFIG` points at an alternate config (error string: "LSFGVK_CONFIG is set but file does not exist: "); a system-wide fallback `/etc/lsfg-vk/conf.toml` exists in the binary.
- Recommendation: give the app a stable binary name and add it to `active_in` (works no matter how the app is launched, no env needed); use `LSFGVK_PROFILE` for terminal-launched debugging. `DISABLE_LSFGVK=1` is the clean off switch (manifest `disable_environment`; empirically verified).

### Q3 — 30fps input → 2x on R9700, latency class

- FG compute cost (official v2.0.0 numbers, https://lsfg-vk.dev/blog/release-v2.0.0/ "Quick look at performance"): RTX 5080, 4x, 2560x1440, 100% flow, no perf mode: **1.60 ms per real frame** (output 2506 fps); laptop Radeon 680M, 2x, 1280x800, 100% flow, no perf mode: **3.30 ms per iteration** (output 605 fps). `lsfg-vk-cli benchmark` measures "Time per iteration" = time per real frame (wiki benchmark page, https://github.com/PancakeTAS/lsfg-vk/wiki/Using-lsfg%E2%80%90vk's-integrated-benchmark). v2 is ~3–3.5x faster and ~74% less VRAM than v1; performance mode is "2x to 8x faster" with slight quality loss (config-options page).
- A local R9700 benchmark was **not possible today** (missing shader DLL → `std::bad_alloc` from `lsfg-vk-cli benchmark`). Class estimate for R9700 (top-tier RDNA4), 2x, 1080p, performance mode: FG compute well under ~1–2 ms per real frame (it must beat the 33.3 ms input period by a huge margin; even the 680M laptop does 2x 720p in 3.3 ms without perf mode).
- End-to-end latency is dominated by FIFO/VBlank, not compute: one real frame + one generated frame per 33.3 ms period = exactly 2 VBlanks at 60Hz (16.67 ms each). Input-to-output ≈ FG compute (≲2 ms) + 1–2 VBlanks of queueing = **~17–35 ms, i.e. 1–2 frames at 60Hz, typical**; worst case bounded by swapchain depth — the wiki notes the FIFO queue can hold up to the swapchain image count ("usually between 3 or 8 images … up to 8 frames of latency"), and the layer deliberately adds +1+intermediate images, so a few extra VBlanks of queue depth are expected, not 8.
- The 30fps→60Hz 2x case is the *ideal* fit for vsync pacing (2 output frames per 2-VBlank period). If our dedup logic ever stalls (two periods without a new frame), the generated output still plays (the layer interpolates per presented frame) but the cadence will visibly hiccup; if input jitter exceeds ~1 VBlank, frames get dropped under vsync.

### Q4 — How to detect the layer actually engaged

Empirically established on this machine (2026-09-08):
- **Definitive engagement signal (app-level, stderr):** the pair of log lines
  ```
  (lsfg-vk) [INFO]: Loaded lsfg-vk layer version 2.0.0
  (lsfg-vk) [INFO]: Using profile with name '<name>' (identified via executable|environment|process name|wine executable|Steam App ID|unknown method)
  ```
  (matching what the Getting Started page, https://lsfg-vk.dev/docs/getting-started/, shows for a working vkcube). Followed by `Hooked new Vulkan instance creation`, `Hooked new Vulkan device creation`, `Initializing lsfg-vk instance with half precision enabled`. The identification suffix tells you *which* mechanism activated it.
- **Self-disable signature:** no `(lsfg-vk)` lines at all (verified with `vulkaninfo`, which matches no profile). The only trace is the cosmetic loader message `ERROR: [Loader Message] Code 0 : loader_create_instance_chain: Failed to find 'vkGetInstanceProcAddr' in layer ".../liblsfg-vk-layer.so"` — reproduced locally on non-matching apps and seen in discussion #474, where the layer was nonetheless active; so **do not use the presence of that message as the disable signal — use the absence of the `(lsfg-vk)` lines** (or `VK_LOADER_DEBUG=layer` for chain presence).
- **Loader-level check (docs, https://lsfg-vk.dev/docs/troubleshooting/basic-troubleshooting-steps/):** run with `VK_LOADER_DEBUG=layer` and look for `VK_LAYER_LSFGVK_frame_generation` between `<Loader>` and `<Device>` — proves the .so is chained. Docs also note the Flatpak-sandbox and `active_in`-misconfiguration branches of this test.
- **Positive/negative controls:** `DISABLE_LSFGVK=1 <app>` (manifest `disable_environment`) → clean run, no layer lines (verified). Visual: the docs note the FG'd cube "will spin slower than usual" vs the disabled control.
- **Durable logs:** `LSFGVK_LOG_LEVEL` (debug/info/warning/error) and `LSFGVK_LOG_FILE` (also hidden config keys `log_level` / `log_file`, useful "in case stderr is swallowed by the application" — config-options page). Note the binary writes shader temp to `/tmp/lsfg-vk`.
- **Hard-failure signal to watch:** `(lsfg-vk) [ERROR]: An error occured while initializing the lsfg-vk swapchain: - The specified shader DLL does not exist` → our app's `vkCreateSwapchainKHR` will fail and the app must handle/abort (vkcube hard-asserts). So "DLL present" is a precondition for a working profile match.

### Q5 — What differs between vkcube and our capture→present app

From the wiki injection mechanics (https://github.com/PancakeTAS/lsfg-vk/wiki/Injecting-frames-into-Vulkan-apps): the layer hooks `vkCreate{Instance,Device,SwapchainKHR}`/`vkDestroy*`, appends its own extensions, adds `TRANSFER_SRC|TRANSFER_DST` to swapchain `imageUsage`, hooks `vkQueuePresentKHR` (copies the presented swapchain image into its own image), and inserts frames by `vkAcquireNextImageKHR` + write + a second `vkQueuePresentKHR`, all paced via the forced FIFO mode; FG runs on a *separate* Vulkan device (Vulkan 1.3, shared memory/semaphores for cross-device image sharing).
Implications / deltas for our app:
1. **We must use a real `VK_KHR_swapchain` + `vkQueuePresentKHR`.** The layer only sees the world through swapchain creation and present calls; a non-WSI app (or present via a secondary path the layer doesn't intercept) would not be FG'd. vkcube is a plain WSI app — same contract, so this is satisfied by design.
2. **Present mode:** vkcube runs FIFO; our app may request anything, but with default `override_present_mode=true` the layer rewrites our swapchain to FIFO. Do not rely on `VK_PRESENT_MODE_IMMEDIATE`.
3. **Image count:** the layer increases the swapchain image count by 1 + intermediate images (and `preserve_swapchain_image_count=false` by default). Our app should request a *sane* `minImageCount` (e.g. 3–4) and not hardcode exotic values; if we hit a game-style crash the docs' `preserve_swapchain_image_count=true` is the documented workaround.
4. **Cadence:** vkcube renders freely at 60+ fps; we present a deduplicated 30 fps stream (33.3 ms period). 2x at 60Hz is a perfect 2-frames-per-2-VBlanks fit, but it's *tight*: input jitter/stalls beyond ~1 VBlank surface as dropped/duplicated output under vsync pacing (docs: "requires Vsync, otherwise frames will be skipped"). The layer has no "dedup" concept — it generates `multiplier-1` frames between *every* presented frame, so we must do the deduplication ourselves before present (present only distinct frames at 30 fps).
5. **Process identity:** vkcube matches via binary name (`vkcube` in `active_in`); our app needs its own `active_in` entry (binary name / process name / path tail) or `LSFGVK_PROFILE` env var. The layer also cross-checks via `/proc/self/comm`/`/proc/self/maps`, so a wrapper/renamed launcher still matches if the final binary name matches.
6. **VRR off, no other Vulkan layers in the chain** (Quirks page), 64-bit, Vulkan (not OpenGL) — all trivially true for our new app.
7. **Failure coupling (new in our situation):** because the shader DLL is currently missing on this machine, *any* app with a matching profile — vkcube included — crashes at swapchain creation (verified). Our app inherits this: before the DLL is restored (`dll` config must point at a real `lsfg-vk.dll`; the docs' troubleshooting note about Steam sandboxing suggests keeping it on the main partition), the known-working precedent is not actually working, and `DISABLE_LSFGVK=1` is the only clean path. This is a prerequisite ticket, not an app-design issue.

## Answer

1. **Present mode:** Use `VK_PRESENT_MODE_FIFO` (or any mode — the layer overrides to FIFO by default via `override_present_mode=true`; do **not** turn that off). `VK_PRESENT_MODE_IMMEDIATE` is not suitable: vsync pacing consumes one image per VBlank, so it "requires Vsync, otherwise frames will be skipped" (pacing-modes docs). Quirks: VRR must be off (unsupported), no other Vulkan layers in the chain. 30fps input + 2x = 2 frames per 2 VBlanks at 60Hz — a perfect but tight fit; keep input cadence stable. `pacing="none"` (our config) is a valid 2.0.0 value (binary accepts exactly `vsync`/`none`) and with default `override_present_mode` still ends up FIFO-paced (CLI reports "Vsync").
2. **Activation:** Use `active_in` with our app's binary name (docs-recommended, hot-reloadable, matches via `/proc/self/exe`/`/proc/self/comm`/`/proc/self/maps`, works however the app is launched); `LSFGVK_PROFILE='<name>'` is the env-var alternative (verified working in 2.0.0 **without** `LSFGVK_ENV=1`, log suffix `(identified via environment)`). A profile with no `active_in` is not auto-activated (verified: non-matching app → self-disable, zero layer lines). `LSFGVK_CONFIG` switches config file; `DISABLE_LSFGVK=1` disables entirely. (v1 `LSFG_*` names in the old wiki are obsolete.)
3. **Latency class (2x, R9700, performance mode):** FG compute ≲1–2 ms per real frame (official v2.0.0 reference: 3.30 ms/iteration for 2x 720p on a 680M laptop *without* perf mode; 1.60 ms/iteration for 4x 1440p on an RTX 5080; perf mode is 2–8x lighter). End-to-end = FG compute + 1–2 VBlanks of FIFO queueing at 60Hz → **~17–35 ms, i.e. 1–2 frames at 60Hz typical**, worst case bounded by the (layer-inflated) swapchain depth rather than the 8-frame v1-era worst case. Local R9700 benchmark not possible today: the shader DLL is missing (see 5), and `lsfg-vk-cli benchmark` fails with `std::bad_alloc`.
4. **Detection:** The authoritative signal is the stderr pair `(lsfg-vk) [INFO]: Loaded lsfg-vk layer version 2.0.0` + `Using profile with name '<name>' (identified via <mechanism>)` (Getting Started page; verified live for both activation mechanisms). Self-disable = **absence of any `(lsfg-vk)` lines** (verified with `vulkaninfo`); the `ERROR: [Loader Message] ... Failed to find 'vkGetInstanceProcAddr' in layer ...liblsfg-vk-layer.so` message is the cosmetic artifact (issue #482 unresolvable — repo has issues disabled; nearest is discussion #474) and appears even in some active runs, so never use it as the disable signal. Loader-level chain proof: `VK_LOADER_DEBUG=layer` → `VK_LAYER_LSFGVK_frame_generation` between `<Loader>` and `<Device>` (docs troubleshooting). For logs: `LSFGVK_LOG_LEVEL` / `LSFGVK_LOG_FILE` (or config `log_level`/`log_file`). Watch for the hard-failure line `- The specified shader DLL does not exist` (app's swapchain creation then fails).
5. **vkcube vs our app:** The layer is swapchain-blind — it hooks `vkCreateSwapchainKHR`/`vkQueuePresentKHR`, forces FIFO, inflates image count by 1+intermediates, and FGs via a separate Vulkan device (wiki injection doc). Deltas: (a) we must use a real WSI swapchain + `vkQueuePresentKHR` (satisfied by design); (b) present mode doesn't matter (layer overrides to FIFO — don't request/depend on immediate); (c) request a sane `minImageCount` (3–4); (d) cadence: vkcube renders at 60+fps freely, we present exactly 30fps distinct frames — 2x at 60Hz fits 2-frames-per-2-VBlanks but input jitter >~1 VBlank → dropped/dup output; we own the dedup (the layer FGs between *every* presented frame, it has no dedup); (e) identity: vkcube matches by binary name; we need our own `active_in` entry or `LSFGVK_PROFILE`; (f) **the precedent is currently broken**: with `dll` pointing at a directory containing no `lsfg-vk.dll`, vkcube crashes at swapchain init (verified 2026-09-08; no `lsfg-vk.dll` exists anywhere on this machine) — restoring a real `lsfg-vk.dll` and pointing `dll` at it is a hard prerequisite before our app can be tested; `DISABLE_LSFGVK=1` is the clean fallback meanwhile.
