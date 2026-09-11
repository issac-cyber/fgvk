# fgvk — real-time frame interpolation for video

**fg** (frame generation) does real-time frame interpolation — inserting generated frames between real ones — for video, using Lossless Scaling's `lsfg-vk` as the frame-generation engine. This is a **personal, single-machine** project, not a library. Every command below assumes the target machine in the footer, and the code is tuned to that one setup.

## Current status — which FG route actually works

The key finding, after a long investigation: **frame generation only works through a real `mpv` window — not inside a browser, and not through a raw screen-capture pipe.**

| Route | State | Why |
|-------|-------|-----|
| `lsfg-vk` FG engine | ✅ alive | `lsfg-vk-cli benchmark` runs 2× normally |
| In-browser (Chromium) FG | ❌ dead | The compositor presents at display refresh (165/174 Hz), so FG is capped and has no effect (confirmed with `--enable-unsafe-swiftshader`) |
| `screen-fg` portal capture | ❌ broken | The source emits one frame then freezes (PipeWire buffer-pool deadlock, xdpw#395; the `SPA_PARAM_Buffers` path is missing in the `libspa-videoconvert` stage) |
| **`mpv` external window + `lsfg-vk`** | ✅ **the usable route** | `mpv --vo=gpu --gpu-api=vulkan` uses a vsync-paced swapchain → the `lsfg-vk` implicit layer intercepts it → interpolation works. The `youtube-fg-extension` component builds on this |

**Bottom line:** the working way to use fgvk is the **`youtube-fg-extension`** (Chrome extension + native host), which launches `mpv` and lets `lsfg-vk` insert frames. `screen-fg`'s present pipeline + layer are still validated (via synthetic mode), but its portal-capture path is broken.

## What the pieces do

| Component | What it is |
|-----------|-----------|
| `screen-fg/` | The main C++ binary: XDG-portal ScreenCast v5 capture → CPU frame dedup (32×32 MAD) → SDL3 fullscreen window + Vulkan FIFO swapchain present → `lsfg-vk` implicit-layer FG. Also exposes a stdio control channel (stdin commands / stdout status JSON / stderr log). Spec: `specs/screen-fg-pipeline/spec.md` (LOCKED v1.0, 2026-09-09; §13 = build-deviation appendix). **Note:** its capture path is currently broken (see status above). |
| `screen-fg-gui/` | A GTK4 (gtkmm-4.0) desktop GUI controller: `fork`+`exec`s `screen-fg`, drives it over the stdio channel, and shows FPS / multiplier / layer / state plus start/stop, pause/resume, and a HUD toggle. Spec: `specs/screen-fg-gui/spec.md`. |
| `shared/` | A pure-function protocol module (`protocol.hpp`) — the **single source** of the stdio control protocol shared by both C++ binaries. No I/O, no external C libraries; both projects compile the same header, so the vocabulary and structs can't drift. `kVersion = 1`. |
| `specs/` | The two specs plus design history (research notes, spec-lock record, shader-container note). |
| `docs/` | Dated design/build plan records (history is never rewritten). |
| `youtube-fg-extension/` | **The usable FG route:** a Chrome extension + native messaging host. The popup picks 2×/3×/4×/10× → the host launches `mpv --vo=gpu --gpu-api=vulkan` with `LSFGVK_PROFILE` + `MESA_VK_DEVICE_SELECT` (GPU1). A background service worker monitors `mpv` and auto-restores the source tab when the mpv window is closed. Also supports `anime1.me` and `hanime1.me` (which yt-dlp can't resolve). |

## How frame generation works (the working route)

`youtube-fg-extension` is the thing to actually use. You click the extension on a web video, pick a multiplier, and it:

1. Launches `mpv --vo=gpu --gpu-api=vulkan <resolved-url>` in an **external window**, pinned to a specific GPU (`MESA_VK_DEVICE_SELECT`) and profile (`LSFGVK_PROFILE` = the chosen 2×/3×/4×/10× profile).
2. Because `mpv` uses a **vsync-paced Vulkan swapchain**, the `lsfg-vk` implicit layer can intercept the presented frames and insert generated ones in between — this is the only route where the FG engine has room to work.
3. The extension mutes the source browser tab and pauses its `<video>` (mpv plays the audio/video, single player → best A/V sync).
4. When you close the `mpv` window (or hit Stop), a background service worker detects it and un-mutes + resumes the source tab, so the audio comes back automatically.

## Build & verify

| Do this | Command |
|---------|---------|
| Build `screen-fg` | `cd screen-fg && cmake -B build && cmake --build build` |
| `screen-fg` pure-module tests (74 checks) | `cd screen-fg && ./build/screen-fg-tests` |
| Build `screen-fg-gui` | `cd screen-fg-gui && cmake -B build && cmake --build build` |
| GUI ProtocolDecoder test | `cd screen-fg-gui && ./build/screen-fg-gui-tests` |
| Portal-free e2e (synthetic) | `cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 ./build/screen-fg` |
| extension host selftest | `youtube-fg-extension/host/fgvk-mpv-launch.py --selftest` |
| extension unit test (node) | `cd youtube-fg-extension && node test/url-utils.test.cjs` |
| extension e2e (manual) | Fully restart Chrome → open any youtube.com video → click the extension → 2× → watch the mpv window; the popup "Last launch" shows success/failure |

**Synthetic e2e expected output:** stderr shows `synthetic 模式` → `結束（3 帧捕捉 / 1 帧呈現）`, exit 0. (The synthetic 16×16 moving block is too small to push the 32×32 MAD above the default threshold 3.0, so dedup only passes the first frame — "1 帧呈現" is the expected value.)

- Add `DISABLE_LSFGVK=1` to turn the layer off (validate the present pipeline only).
- Leave it unset to validate the `lsfg-vk` layer together with the pipeline.

**System dependencies**

- `screen-fg`: `libsdl3-dev libpipewire-0.3-dev libvulkan-dev libglib2.0-dev` (the portal goes through GDBus / gio-2.0, not raw libdbus).
- `screen-fg-gui`: `libgtkmm-4.0-dev` (no libadwaita — there's no C++ binding).
- Layer prerequisite: keep Steam Lossless Scaling on the `lsfg-vk` update channel.

## The stdio control protocol

The control contract between `screen-fg` and `screen-fg-gui` lives in **one place** — `shared/protocol.hpp` (`kVersion = 1`). Both binaries compile the same header, so the vocabulary and structs can't drift.

- **Commands** (GUI → `screen-fg` stdin, line-based): `pause` / `resume` / `hud 0|1` / `quit`
- **Status** (`screen-fg` → stdout, JSON lines): `{"type":"status","fps":N,"mult":N,"layer":bool,"state":"running|paused|exiting"}` and `{"type":"exit","code":N}`
- `screen-fg`'s stdout carries **only** protocol JSON lines; all logging goes to stderr (the GUI collects it into its log area).
- A missing or malformed field in `parse` → `nullopt` (explicit skip, **never a silent default**).
- Pause/resume is a **re-exec** on the `screen-fg` side (the Vulkan layer is a per-process global single-instance, so it can't be toggled at runtime); the GUI just sends the command and does not restart the process.

## Config & runtime files

| File | Purpose |
|------|---------|
| `~/.config/screen-fg/config.toml` | `screen-fg` persistent config (flat-TOML subset; missing file = pure defaults). Keys: see `screen-fg/AGENTS.md`. |
| `~/.config/lsfg-vk/conf.toml` | `lsfg-vk` layer config. Currently holds 4 FG profiles (2×/3×/4×/10× / 100%, multiplier 2/3/4/10, vsync + override_present_mode + performance_mode); the extension picks one via `LSFGVK_PROFILE` (no `active_in` needed). ⚠️ The `lsfg-vk` GUI (the Lossless Scaling app) **overwrites this file on save** — saving in the GUI wipes these 4 profiles; re-add them in the GUI (or don't save in the GUI). Backups: `conf.toml.bak-btn4`, `conf.toml.bak-ext`, `conf.toml.bak-screenfg`. |
| `~/.local/share/vulkan/implicit_layer.d/` | Where the `lsfg-vk` implicit layer (`VK_LAYER_LSFGVK_frame_generation`) lives; `screen-fg` warns at boot if it's not enumerated. |
| `~/.local/share/applications/screen-fg-gui.desktop` | The GUI desktop icon (install steps in `screen-fg-gui/AGENTS.md`). |

## Environment variables

| Variable | Used by | Purpose |
|----------|---------|---------|
| `SCREENFG_HUD` | `screen-fg` | `"1"`/`"0"` overrides config `hud`; the re-exec preserves the runtime HUD state. |
| `SCREENFG_DISPLAY` | `screen-fg` | Overrides config `display` (screen index the FG window covers; the GPU follows it). |
| `SCREENFG_STATE` | `screen-fg` | `"paused"` → boot into passthrough (re-exec carries the env). Note: the re-exec env vocabulary is `"normal"`/`"paused"`, distinct from the status JSON `"running"`/`"paused"` — don't mix them. |
| `SCREENFG_SYNTHETIC` | `screen-fg` | Non-`"0"` = synthetic frames (no portal/picker; validate present pipeline + layer). |
| `SCREENFG_SYNTHETIC_FRAMES` | `screen-fg` | Synthetic frame count (default 300). |
| `DISABLE_LSFGVK` | `lsfg-vk` layer | `"1"` = disable the layer (validate present pipeline only). |
| `DBUS_SESSION_BUS_ADDRESS` | `screen-fg` | Must be a clean bus path (a stale guid in the env → "Did not receive a reply"). |
| `SCREENFG_BIN` | `screen-fg-gui` | The `screen-fg` binary path (first priority in the search order). |
| `LSFGVK_PROFILE` | `lsfg-vk` layer | Select a profile by name (the extension host uses it to set 2×/3×/4×/10×; no `active_in` needed). |
| `MESA_VK_DEVICE_SELECT` | Mesa device-select layer | Pin a specific GPU. The extension host uses `1002:7551:0000:07:00.0` = GPU1 / the second R9700; both R9700s share device id `1002:7551`, so the PCI BDF is required to disambiguate. |

## Naming

`fgvk` is the **project/directory name**. The binary is still called **`screen-fg`** (the `screen-fg-plain` copy, `~/.config/screen-fg/`, and the layer `active_in="screen-fg"` all stay). When searching the code, search for **`screen-fg`**, not `fgvk`.

## Where to go deeper

- **Per-component details, build steps, and hard-won pitfalls** live in each component's own `AGENTS.md` — the top-level one, plus `screen-fg/AGENTS.md`, `screen-fg-gui/AGENTS.md`, `youtube-fg-extension/AGENTS.md`, and `shared/AGENTS.md`.
- **Human explainer** (pipeline diagram, 30-second overview, design-decision log): `explainer.html`.
- **Locked specs + design history**: `specs/` (research notes, spec-lock record, shader-container note).
- **Dated build/design plan records**: `docs/`.

---
*Target machine: Ubuntu 26.04 · GNOME Wayland · 2× AMD R9700 (each driving one 3440×1440 display) · `lsfg-vk` 2.0.0 (shader container restored).*
