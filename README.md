# fgvk — real-time frame interpolation for video

**fg** (frame generation) does real-time frame interpolation — inserting generated frames between real ones — for video, using Lossless Scaling's `lsfg-vk` as the frame-generation engine. This is a **personal, single-machine** project, and every command below assumes the target machine in the footer.

## The working route

The working route is the **`youtube-fg-extension`** — a Chrome extension + native messaging host that takes a web video and plays it in an external `mpv` window, where `lsfg-vk` inserts the generated frames.

1. You click the extension on a web video and pick a multiplier (2×/3×/4×/10×).
2. The extension launches `mpv --vo=gpu --gpu-api=vulkan <resolved-url>` in an **external window**, pinned to a specific GPU (`MESA_VK_DEVICE_SELECT`) and FG profile (`LSFGVK_PROFILE`).
3. Because `mpv` uses a vsync-paced Vulkan swapchain, the `lsfg-vk` implicit layer intercepts the presented frames and inserts generated ones in between.
4. The extension mutes the source browser tab and pauses its `<video>` (mpv plays the audio, single player → best A/V sync). A background service worker monitors `mpv` and, when you close the window (or hit Stop), un-mutes + resumes the tab so the audio comes back automatically.

It also supports `anime1.me` and `hanime1.me` (which yt-dlp can't resolve).

## Why this route (not a browser or screen-capture one)

Frame generation only works where the layer has room to insert frames. A long investigation showed:

| Route | State | Why |
|-------|-------|-----|
| `lsfg-vk` FG engine | ✅ alive | `lsfg-vk-cli benchmark` runs 2× normally |
| In-browser (Chromium) FG | ❌ dead | The compositor presents at display refresh (165/174 Hz), so FG is capped and has no effect |
| Screen-capture → `screen-fg` (XDG portal ScreenCast) | ❌ dead end | The source emits one frame then freezes (PipeWire buffer-pool deadlock, xdpw#395). The former `screen-fg` / `screen-fg-gui` / `shared` components were removed from this repo (2026-09-11); their design history remains in `docs/` and `explainer.html`. |
| **`mpv` external window + `lsfg-vk`** | ✅ **the route this repo uses** | `mpv --vo=gpu --gpu-api=vulkan` uses a vsync-paced swapchain → the `lsfg-vk` implicit layer intercepts it → interpolation works |

## What's in the repo

| Path | What it is |
|------|-----------|
| `youtube-fg-extension/` | **The working FG route** (Chrome extension + native messaging host). See `youtube-fg-extension/AGENTS.md` for details, build, and pitfalls. |
| `docs/` | Dated design/build plan records (including the historical `screen-fg` build and the extension design). History is never rewritten. |
| `explainer.html` | A standalone HTML explainer (the historical `screen-fg` pipeline). |

## Build & verify

| Do this | Command |
|---------|---------|
| extension host selftest | `youtube-fg-extension/host/fgvk-mpv-launch.py --selftest` |
| extension unit test (node) | `cd youtube-fg-extension && node test/url-utils.test.cjs` |
| profile is selectable | `LSFGVK_PROFILE="2x FG / 100%" mpv --vo=gpu --gpu-api=vulkan <clip>` → log shows `Using profile '2x FG / 100%' (identified via environment)` |
| extension e2e (manual) | Fully restart Chrome → open any youtube.com video → click the extension → 2× → watch the mpv window; the popup "Last launch" shows success/failure |

**System dependencies:** `mpv` + `yt-dlp` (installed); the extension's native host is pure Python (stdlib only, no third-party packages).

**Layer prerequisite:** keep Steam Lossless Scaling on the `lsfg-vk` update channel. Four FG profiles (2×/3×/4×/10×) must exist in `~/.config/lsfg-vk/conf.toml`; the host auto-heals missing ones on launch.

## Config & runtime files

| File | Purpose |
|------|---------|
| `~/.config/lsfg-vk/conf.toml` | `lsfg-vk` layer config. Holds 4 FG profiles (2×/3×/4×/10× / 100%, multiplier 2/3/4/10). The extension picks one via `LSFGVK_PROFILE`. ⚠️ The Lossless Scaling GUI **overwrites this file on save** — saving in the GUI wipes the 4 profiles; re-add them (or don't save in the GUI). |
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

- **Extension details, build, and pitfalls:** `youtube-fg-extension/AGENTS.md`.
- **Dated design/build plan records:** `docs/`.
- **Human explainer (historical `screen-fg` pipeline):** `explainer.html`.

---
*Target machine: Ubuntu 26.04 · GNOME Wayland · 2× AMD R9700 (each driving one 3440×1440 display) · `lsfg-vk` 2.0.0 (shader container restored).*
