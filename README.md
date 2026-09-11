# fgvk — real-time frame interpolation for video

**fg** (frame generation) does real-time frame interpolation — inserting generated frames between real ones — for video, using Lossless Scaling's `lsfg-vk` as the frame-generation engine. This is a **personal, single-machine** project; every command below assumes the target machine in the footer.

## How it works

**Frame generation** (frame interpolation) inserts *generated* frames between the real ones, so 24 fps content plays at ~48 / 72 / 96 fps. fgvk does this on the GPU using **Lossless Scaling's `lsfg-vk`**, which is a Vulkan **implicit layer**.

A Vulkan implicit layer sits between an application and the GPU driver. `lsfg-vk` intercepts the app's `vkQueuePresentKHR` calls (the "present these frames" step) and, where there is room, generates and presents extra frames in between. Two things must be true for it to work:

- The app must present through **Vulkan** (so the layer can see the present calls).
- The app must present on a **vsync-paced swapchain** — i.e. it must *not* already be filling every display frame. If the app already presents at the display refresh rate, there's no room to add frames and FG has no effect.

That's why the route matters:

- A **browser** (Chromium) presents at the display refresh (165/174 Hz) → no room → FG is capped and does nothing.
- A **screen-capture pipe** (`screen-fg`) freezes after one frame (PipeWire buffer-pool deadlock, xdpw#395) → dead end (removed from this repo).
- An **`mpv` external window** with `--vo=gpu --gpu-api=vulkan` presents on a vsync-paced swapchain → the layer has room → **FG works**.

So the working route wraps that known-good `mpv + lsfg-vk` combination in a one-click Chrome extension:

1. You click the extension on a web video and pick a multiplier (2×/3×/4×/10×).
2. The extension launches `mpv --vo=gpu --gpu-api=vulkan <resolved-url>` in an **external window**, pinned to a specific GPU (`MESA_VK_DEVICE_SELECT`) and an `lsfg-vk` FG profile (`LSFGVK_PROFILE`).
3. `mpv` presents through the vsync-paced swapchain; the `lsfg-vk` layer intercepts the presents and inserts the generated frames.
4. The extension mutes the source browser tab and pauses its `<video>` (mpv plays the audio — single player, best A/V sync). A background service worker monitors `mpv` and, when you close the window or hit Stop, un-mutes + resumes the tab so the audio comes back automatically.

It also supports `anime1.me` and `hanime1.me` (which yt-dlp can't resolve) via site-specific resolvers in the native host.

## What to install

**Target machine:** Ubuntu 26.04 · GNOME Wayland · 2× AMD R9700 (each driving a 3440×1440 display). The GPU is pinned to GPU1 via `MESA_VK_DEVICE_SELECT`.

**Prerequisites**

| Package | Why |
|---------|-----|
| **Steam Lossless Scaling** (on the `lsfg-vk` update channel) | Provides the `lsfg-vk` FG engine as a Vulkan implicit layer; it must be present at `~/.local/share/vulkan/implicit_layer.d/VK_LAYER_LSFGVK_frame_generation.json`. |
| **mpv** with Vulkan (installed on the target machine) | The external window that presents on a vsync-paced swapchain (`--vo=gpu --gpu-api=vulkan`). |
| **yt-dlp** (installed on the target machine) | Resolves stream-site URLs (YouTube, Bilibili, …). |
| **Google Chrome** | Hosts the extension. |
| **4 FG profiles** (2×/3×/4×/10×) in `~/.config/lsfg-vk/conf.toml` | One per multiplier. The host auto-heals missing ones on launch, so you can skip this if needed. ⚠️ The Lossless Scaling GUI **overwrites `conf.toml` on save** — saving in the GUI wipes the 4 profiles. |

**One-time setup**

```bash
# 1. Install the native host (pure Python, stdlib only)
mkdir -p ~/.local/bin
cp youtube-fg-extension/host/fgvk-mpv-launch.py ~/.local/bin/
chmod +x ~/.local/bin/fgvk-mpv-launch.py

mkdir -p ~/.config/google-chrome/NativeMessagingHosts
cp youtube-fg-extension/host/com.fgvk.host.json ~/.config/google-chrome/NativeMessagingHosts/
```

Confirm the host manifest's `path` points to the **absolute path** of `fgvk-mpv-launch.py` and `allowed_origins` is `["chrome-extension://jggfcgmdjhdeokfjlmnfdmnekodggdjc/"]` (the extension ID is locked by the `key` in `manifest.json`, so it's fixed).

Then:

1. `chrome://extensions` → **Developer mode** → **Load unpacked** → select the `youtube-fg-extension/` directory.
2. **Fully restart Chrome** (the native-host manifest is only read at startup).

**Verify it's wired up:** run `youtube-fg-extension/host/fgvk-mpv-launch.py --selftest`, then open a YouTube video → click the extension → 2× → the mpv window should appear and be frame-generated.

## Why this route (not a browser or screen-capture one)

Frame generation only works where the layer has room to insert frames. A long investigation showed:

| Route | State | Why |
|-------|-------|-----|
| `lsfg-vk` FG engine | ✅ alive | `lsfg-vk-cli benchmark` runs 2× normally |
| In-browser (Chromium) FG | ❌ dead | The compositor presents at display refresh (165/174 Hz), so FG is capped and has no effect |
| Screen-capture → `screen-fg` (XDG portal ScreenCast) | ❌ dead end | The source emits one frame then freezes (PipeWire buffer-pool deadlock, xdpw#395); removed from this repo 2026-09-11 |
| **`mpv` external window + `lsfg-vk`** | ✅ **the route this repo uses** | vsync-paced swapchain → the `lsfg-vk` layer intercepts the presents → interpolation works |

## What's in the repo

| Path | What it is |
|------|-----------|
| `youtube-fg-extension/` | **The working FG route** (Chrome extension + native messaging host). See `youtube-fg-extension/AGENTS.md` for details and pitfalls. |
| `docs/` | Dated design/build plan records (including the historical `screen-fg` build and the extension design). History is never rewritten. |
| `explainer.html` | A standalone HTML explainer (the historical `screen-fg` pipeline). |

## Build & verify

| Do this | Command |
|---------|---------|
| extension host selftest | `youtube-fg-extension/host/fgvk-mpv-launch.py --selftest` |
| extension unit test (node) | `cd youtube-fg-extension && node test/url-utils.test.cjs` |
| profile is selectable | `LSFGVK_PROFILE="2x FG / 100%" mpv --vo=gpu --gpu-api=vulkan <clip>` → log shows `Using profile '2x FG / 100%' (identified via environment)` |
| extension e2e (manual) | Fully restart Chrome → open a YouTube video → click the extension → 2× → watch the mpv window; the popup "Last launch" shows success/failure |

## Config & runtime files

| File | Purpose |
|------|---------|
| `~/.config/lsfg-vk/conf.toml` | `lsfg-vk` layer config. Holds the 4 FG profiles (2×/3×/4×/10× / 100%, multiplier 2/3/4/10, vsync + override_present_mode + performance_mode). The extension picks one via `LSFGVK_PROFILE`. ⚠️ The Lossless Scaling GUI **overwrites this file on save**. |
| `~/.local/share/vulkan/implicit_layer.d/` | Where the `lsfg-vk` implicit layer (`VK_LAYER_LSFGVK_frame_generation`) lives. |
| `~/.config/fgvk/` | Extension host runtime dir: mpv PID (`mpv.pid`) + mpv log. |
| `~/.local/bin/fgvk-mpv-launch.py` | The extension native host (installed, `chmod +x`). |
| `~/.config/google-chrome/NativeMessagingHosts/com.fgvk.host.json` | The extension native-host manifest (`allowed_origins` = the extension ID). |

## Environment variables

| Variable | Used by | Purpose |
|----------|---------|---------|
| `LSFGVK_PROFILE` | `lsfg-vk` layer | Select a profile by name (the host uses it to set 2×/3×/4×/10×; no `active_in` needed). |
| `MESA_VK_DEVICE_SELECT` | Mesa device-select layer | Pin a GPU. The host uses `1002:7551:0000:07:00.0` = GPU1 / the second R9700; both R9700s share device id `1002:7551`, so the PCI BDF disambiguates. |
| `DISABLE_LSFGVK` | `lsfg-vk` layer | `"1"` = disable the layer. |

## Where to go deeper

- **Extension details, build, and pitfalls:** `youtube-fg-extension/AGENTS.md` (and `youtube-fg-extension/install.md`).
- **Dated design/build plan records:** `docs/`.
- **Human explainer (historical `screen-fg` pipeline):** `explainer.html`.

---
*Target machine: Ubuntu 26.04 · GNOME Wayland · 2× AMD R9700 (each driving a 3440×1440 display) · `lsfg-vk` 2.0.0 (shader container restored).*
