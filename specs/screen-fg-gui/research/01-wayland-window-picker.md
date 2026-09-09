# 01 — Can a GUI app show its OWN "pick a window" UI on GNOME Wayland?

**Ticket 01 — screen-fg GUI wayfinder map. Research done 2026-09-09.**

## Feasibility verdict

**INFEASIBLE (natively)** for a true in-app window picker on GNOME Wayland (gnome-shell / mutter).
**PARTIALLY FEASIBLE** only via a custom **GNOME Shell extension** that the user installs.
**FEASIBLE** to keep the **XDG portal ScreenCast picker** as the selection UI (what screen-fg uses today).

There is **no standard Wayland protocol that lets one client enumerate other clients' windows on mutter/gnome-shell.** That is precisely why the portal picker exists. On this machine (GNOME Wayland, not wlroots) the only *supported* picker is the one gnome-shell itself renders; an app cannot build its own window list or its own hover-pick overlay over other windows.

---

## Context (screen-fg GUI)

- screen-fg (C++/SDL3, this machine = GNOME Wayland / gnome-shell / mutter, Ubuntu 26.04, 2×R9700) currently captures a chosen browser window via **XDG portal ScreenCast v5** (GDBus → PipeWire).
- The standalone GUI (MVP feature #3, "內嵌選窗") wants to **replace the system portal dialog with the app's own picker**.
- This ticket is the map's flagged key uncertainty: *Wayland's security model generally does not let one client enumerate other clients' windows.*

## Q1 — Wayland security model: what a client can/can't see

Wayland is a **compositing** protocol: the display server is the *compositor* (here, **mutter embedded in gnome-shell**), which owns all rendering and input dispatch. The core consequence:

> "applications cannot see or interfere with other clients. They are only aware of their own windows and receive input events only when they are in focus, as determined by the compositor."

> "Applications cannot see what is outside their own surfaces; they do not even know what else is on the screen."

(Sources: openlib.io Wayland security-model article; corroborated by the `wayland-screenshot` project README and the StackExchange "Debian 13 … most Wayland automation" thread.)

So:
- A Wayland client **can** read its **own** surfaces, and (with user consent) capture the **whole screen/monitor** via the portal + PipeWire.
- A Wayland client **cannot** enumerate **other** clients' windows, cannot hit-test "which window is at (x,y)", and cannot read/draw onto other clients' buffers.
- This is by design (anti-keylogging, anti-spoofing, consent-driven capture). It is a **hard architectural boundary**, not a missing feature to toggle on.

**Why the portal picker exists:** because only the *compositor* (gnome-shell) has access to every window. The portal mediates the consent + the choice, and gnome-shell draws the chooser itself. The app-facing portal API has **no** "list windows" or "pick" method — see Q2.

## Q2 — Mechanisms for "pick a window" on GNOME Wayland

App-facing **XDG portal ScreenCast** spec (v5 used by screen-fg; v6 current). The app-side API is only:

`CreateSession()` → `SelectSources(types=WINDOW, multiple=…)` → `Start(parent_window)` → `OpenPipeWireRemote()`.

`Start()` "will typically result the portal presenting a dialog letting the user do the selection." There is **no** method for the app to (a) get a window list or (b) render a picker. The chooser is drawn by the portal *implementation*. On GNOME that is **gnome-shell** (the `org.gnome.Mutter.ScreenCast` / `org.gnome.Shell` D-Bus path — confirmed running on this machine; the `xdg-desktop-portal-gnome` backend is a thin wrapper over those GNOME D-Bus interfaces).

Wayland **protocols** that would let a client enumerate windows (and the **compositor support**, per Wayland Explorer's per-compositor tracking table):

| Protocol | wlroots (Sway/Hyprland/niri/Labwc/river/Mir) | **mutter (GNOME)** | KWin (KDE) |
|---|---|---|---|
| `ext-foreign-toplevel-list-v1` (freedesktop, staging) | ✅ supported | **✗ not supported (mutter 51)** | ✗ (KDE uses its own protocols) |
| `wlr-foreign-toplevel-management-unstable-v1` (wlroots) | ✅ supported | **✗ not supported** | ✗ not supported |
| `zwlr_layer_shell` (layer-shell, wlroots) | ✅ supported | **✗ not in upstream mutter** (only via the `mutter-layer-shell` **fork**) | ✗ (KDE has own layer protocol) |

Key facts:
- `ext-foreign-toplevel-list` / `wlr-foreign-toplevel-management` "is emitted for **all** toplevels, **regardless of the app that has created them**" — exactly the enumeration a picker needs. But **mutter does not implement either** (and KDE/KWin doesn't implement the wlr one — it ships its own "KDE plasma window management" instead).
- The protocol spec itself notes: "The compositor **may choose** to restrict this protocol to a special client launched by the compositor itself or expose it to all clients, this is **compositor policy**." — mutter's policy is to not expose it.
- **layer-shell** is not in upstream mutter — the only way to have it on GNOME is the community `Caellian/mutter-layer-shell` fork (non-default). A picker does not need layer-shell, but it confirms mutter intentionally ships a minimal extension set.
- GNOME/mutter "exposes less to applications than other compositors … there is no layer-shell … and **no API reports window [list]**" (Unisic compositor comparison). The community ask for "official GNOME-supported ways" to do this (StackExchange) is itself the evidence there isn't a first-party one.

**gnome-shell D-Bus**: the shell exposes `org.gnome.Shell`, `org.gnome.Mutter.*` (DisplayConfig, ScreenCast, RemoteDesktop, InputCapture), `org.gnome.Shell.Screenshot`, etc. These are used by **extensions** and the portal backend. There is **no stable, public D-Bus API for an arbitrary app to enumerate windows or trigger a custom picker.** (App-facing `gdbus introspect` of the portal `Desktop` object shows **no `Pick` method** — the chooser is inside gnome-shell.) So D-Bus is a path for a *trusted, in-shell extension*, not for a normal client.

## Q3 — Can an app present its OWN picker? What would it require

To build a true in-app picker (enumerate windows, show them, hover/click to select) on **mutter/gnome-shell**, an app would need **one** of:

1. **A compositor protocol to enumerate foreign toplevels** (`ext-foreign-toplevel-list` or `wlr-foreign-toplevel`) — **not available on mutter.** ❌
2. **layer-shell** (to place a pick overlay) — **not in upstream mutter.** ❌
3. **A gnome-shell D-Bus "list windows / activate" method** — **no stable public API** for normal clients. ❌
4. **Hit-test "window at (x,y)"** — no client-facing API on mutter. ❌ (So you can't even map a pixel-click to a window.)

The **only** real path is **#5: a custom GNOME Shell extension** that runs *inside* gnome-shell (so it has compositor privileges to enumerate windows), and either (a) returns a window list over D-Bus for the app to render, or (b) renders its own picker. This is **partially feasible** but requires the **user to install a third-party extension** (trust + maintenance), and it is **not a standard/supported mechanism** — it will break across gnome-shell upgrades.

**Pseudo-picker fallback (no window identity):** the app *can* capture the **full monitor** via the portal (consented) and let the user **draw a rectangle** on its own canvas (region-pick, not window-pick). This changes capture semantics — screen-fg would capture the **monitor stream + crop** to the rectangle instead of a **per-window** PipeWire stream. It is feasible today and stays 100% Wayland-native, but it is a *degraded* "pick a region", not "pick a window" (loses per-window identity, includes adjacent windows in the frame, no DPI/coord caveats).

## Q4 — Ranked options (feasibility on GNOME Wayland / mutter)

| # | Option | Verdict on GNOME Wayland | Notes |
|---|---|---|---|
| 1 | **Keep the XDG portal ScreenCast picker** | ✅ **Feasible / recommended** | Zero new infra; what screen-fg already does. Not "the app's own UI", but the only *supported* picker. |
| 2 | **Custom GNOME Shell extension** (D-Bus list / custom chooser) | 🟡 **Partially feasible** | The only true "in-app" path. Requires user to install a third-party extension; non-standard, breaks on gnome-shell upgrades. Mark v2 / optional. |
| 3 | **Full-screen portal capture + region crop** (pseudo-picker) | 🟡 **Feasible (degraded)** | Wayland-native, no extension. But "pick a region" not "pick a window"; changes capture semantics (monitor stream + crop). |
| 4 | **Switch compositor to wlroots** (Sway/Hyprland) | ❌ Infeasible here | `ext`/`wlr` foreign-toplevel + layer-shell work there. Out of scope — this machine is GNOME. |
| 5 | **X11 / XWayland fallback** (Xlib `XQueryTree`) | ❌ Not Wayland-native | Works, but abandons the Wayland goal and is a step back. |

## Recommended mechanism

**For MVP: keep the XDG portal ScreenCast picker for window selection.** The GUI's "pick window" button simply triggers the existing `CreateSession → SelectSources(WINDOW) → Start → OpenPipeWireRemote` flow. This is the only *supported, reliable* path on GNOME Wayland, needs no new infrastructure, and screen-fg's capture pipeline is already built on it. The app should present its own UI for *everything else* (start/stop, status, settings) and just hand the window-choice step to the portal — a small, honest "Select window… (system picker)" button rather than a false in-app picker.

**If a bespoke in-app picker is a hard requirement (v2):** the only real mechanism is a **GNOME Shell extension** exposing a D-Bus "list windows" (and optionally "activate") API that the app renders. Budget: user installs the extension; it is non-standard and will need re-verification on every gnome-shell upgrade. Do **not** promise a pure in-process picker — it is not possible on mutter.

## Key constraints (for the map / later tickets)

- **The picker cannot be done in-process.** Any in-app picker either delegates to the portal (option 1) or depends on a **user-installed gnome-shell extension** (option 2). Treat "own picker" as a v2/optional feature with an extension dependency, or scope it out for MVP.
- **No window enumeration / no hit-test for a normal Wayland client on mutter** — this blocks "draw a box and resolve it to a window" entirely; region-pick (option 3) is the only in-process alternative and it loses per-window identity.
- **The portal `Desktop` object exposes no `Pick` method** — the chooser is inside gnome-shell. (Local evidence: `gdbus introspect` of `org.freedesktop.portal.Desktop` shows no Pick; `org.gnome.Mutter.ScreenCast` / `org.gnome.Shell` D-Bus services are running; `xdg-desktop-portal{,-gnome,-gtk}` services are active; no wlroots tooling — `slurp`/`grim`/`wl-list` absent.)
- **D-Bus is an extension-only path, not a public client API** — do not design the GUI around an unsupported `org.gnome.Shell` window-listing call; it will not survive upgrades.

## Local machine evidence (GNOME Wayland)

- `XDG_SESSION_TYPE=wayland`, `XDG_CURRENT_DESKTOP=ubuntu:GNOME`, running `gnome-shell` + `mutter-x11-fram` (X11 helper; X11 off by default in recent GNOME).
- D-Bus names present: `org.gnome.Mutter.ScreenCast`, `org.gnome.Mutter.RemoteDesktop`, `org.gnome.Mutter.InputCapture`, `org.gnome.Shell`, `org.gnome.Shell.Screenshot`, `org.freedesktop.portal.Desktop`, `org.freedesktop.impl.portal.desktop.gnome`.
- `systemctl`: `xdg-desktop-portal.service`, `xdg-desktop-portal-gnome.service`, `xdg-desktop-portal-gtk.service` all active/running.
- **No** wlroots tooling installed: `slurp`, `grim`, `wl-list`, `wlr-randr`, `wayland-info` all absent. (So the wlroots picking path is not even available here.)

## Sources

- Wayland security model (clients can't see other clients; consent-driven capture via portals): https://openlib.io/security-model-wayland-vs-xorg-in-linux/
- `ext-foreign-toplevel-list-v1` spec + per-compositor support table (mutter = ✗): https://wayland.app/protocols/ext-foreign-toplevel-list-v1 (mirror: https://wayland.emersion.fr/protocol/ext-foreign-toplevel-list-v1.html)
- `wlr-foreign-toplevel-management-unstable-v1` spec + per-compositor support table (mutter = ✗): https://wayland.app/protocols/wlr-foreign-toplevel-management-unstable-v1
- `wayland-protocols` 1.32 announcement (ext-foreign-toplevel-list, security-context): https://lists.freedesktop.org/archives/wayland-devel/2023-July/042836.html
- XDG portal **ScreenCast** spec (v6; CreateSession/SelectSources/Start/OpenPipeWireRemote — no pick/list method): https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html
- `xdg-desktop-portal-gnome` (GNOME portal backend = thin wrapper over `org.gnome.Shell` / `org.gnome.Mutter` D-Bus): https://github.com/GNOME/xdg-desktop-portal-gnome
- `xdg-desktop-portal` (portals = secure mediation for confined + normal apps): https://github.com/flatpak/xdg-desktop-portal
- StackExchange — "Debian 13 / GNOME 48 … Does GNOME/Mutter intentionally not implement … foreign-toplevel, etc.?": https://unix.stackexchange.com/questions/800680/
- StackOverflow — "How do I get the active window on Gnome Wayland?" (KWin note re wlr-foreign-toplevel): https://stackoverflow.com/questions/45465016/
- `wayland-screenshot` — "On GNOME Wayland, screen capture is brokered by xdg-desktop-portal … there is no global 'the screen' object": https://github.com/dtg01100/wayland-screenshot
- LXQt wiki — taskbar requires compositor `wlr-foreign-toplevel-management` support: https://lxqt-project.org/wiki/Wayland-Session.html
- `Caellian/mutter-layer-shell` — layer-shell is a **fork** of mutter (not upstream): https://github.com/Caellian/mutter-layer-shell
- Unisic compositor comparison (mutter exposes less: no layer-shell, no window-list API): https://unisic.app/docs/compositors/
- Wayland protocol documentation (official): https://wayland.freedesktop.org/docs/html/
