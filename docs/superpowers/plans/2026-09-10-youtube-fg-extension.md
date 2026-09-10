# YouTube FG Extension Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 一個 Chrome 擴充套件：YouTube 影片頁按一下，用 mpv + lsfg-vk 開起來補幀（2x/3x/4x 可選）。

**Architecture:** unpacked MV3 擴充套件（popup 三顆按鈕）→ Chrome Native Messaging → 一個 Python host 讀 `{url, multiplier}` → 以 `LSFGVK_PROFILE="Nx FG / 100%"` 環境變數 background 啟動 `mpv --vo=gpu --gpu-api=vulkan <url>`。純邏輯（URL 驗證、倍率→profile 映射）抽成可單測函數。

**Tech Stack:** HTML/JS（popup）、Python 3（host）、bash（安裝）、Native Messaging 協議、lsfg-vk 2.0.0 profiles。

---

## 檔案結構

```
youtube-fg-extension/
├── manifest.json            # MV3：permissions + host_permissions
├── popup.html               # 2x/3x/4x 按鈕 + 狀態
├── url-utils.js             # 純函數 isYouTubeWatchUrl + multiplierToProfile（瀏覽器/Node 通用）
├── popup.js                 # 讀 tab、發 Native Messaging、更新狀態
├── test/
│   └── url-utils.test.cjs   # Node 測試（assert，免依賴）
├── host/
│   ├── fgvk-mpv-launch.py   # Native Messaging host
│   └── com.fgvk.host.json   # host manifest
└── install.md               # 一次性安裝 + lsfg-vk profile 設定
```

---

### Task 1: url-utils.js（純邏輯）+ 測試

**Files:**
- Create: `youtube-fg-extension/url-utils.js`
- Test: `youtube-fg-extension/test/url-utils.test.cjs`

- [ ] **Step 1: 寫測試（先失敗）**

```js
// test/url-utils.test.cjs
const assert = require("assert");
const { isYouTubeWatchUrl, multiplierToProfile } = require("../url-utils.js");

// isYouTubeWatchUrl
assert.strictEqual(isYouTubeWatchUrl("https://www.youtube.com/watch?v=abc"), true);
assert.strictEqual(isYouTubeWatchUrl("https://youtube.com/watch?v=abc"), true);
assert.strictEqual(isYouTubeWatchUrl("https://www.youtube.com/watch?v=abc&t=10"), true);
assert.strictEqual(isYouTubeWatchUrl("https://www.youtube.com/"), false);          // 非 watch
assert.strictEqual(isYouTubeWatchUrl("https://www.youtube.com/results?search_query=x"), false);
assert.strictEqual(isYouTubeWatchUrl("https://example.com/watch?v=abc"), false);    // 非 youtube
assert.strictEqual(isYouTubeWatchUrl(""), false);
assert.strictEqual(isYouTubeWatchUrl(null), false);
assert.strictEqual(isYouTubeWatchUrl("not a url"), false);

// multiplierToProfile
assert.strictEqual(multiplierToProfile(2), "2x FG / 100%");
assert.strictEqual(multiplierToProfile(3), "3x FG / 100%");
assert.strictEqual(multiplierToProfile(4), "4x FG / 100%");
assert.strictEqual(multiplierToProfile(5), null);
assert.strictEqual(multiplierToProfile("x"), null);

console.log("url-utils tests passed");
```

- [ ] **Step 2: 跑測試確認失敗**

Run: `node test/url-utils.test.cjs`
Expected: FAIL（`Cannot find module '../url-utils.js'`）

- [ ] **Step 3: 寫 url-utils.js**

```js
// url-utils.js — 純函數，瀏覽器（全域）與 Node（CommonJS）皆可用
function isYouTubeWatchUrl(url) {
  if (typeof url !== "string" || url === "") return false;
  try {
    const u = new URL(url);
    return (u.hostname === "www.youtube.com" || u.hostname === "youtube.com") &&
           u.pathname === "/watch" &&
           !!u.searchParams.get("v");
  } catch (e) {
    return false;
  }
}

function multiplierToProfile(mult) {
  const n = Number(mult);
  if (n !== 2 && n !== 3 && n !== 4) return null;
  return `${n}x FG / 100%`;
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = { isYouTubeWatchUrl, multiplierToProfile };
}
```

- [ ] **Step 4: 跑測試確認通過**

Run: `node test/url-utils.test.cjs`
Expected: PASS（印出 `url-utils tests passed`）

- [ ] **Step 5: Commit**

```bash
git add youtube-fg-extension/url-utils.js youtube-fg-extension/test/url-utils.test.cjs
git commit -m "feat(extension): url-utils 純函數 + 測試"
```

---

### Task 2: Native Messaging host（Python）

**Files:**
- Create: `youtube-fg-extension/host/fgvk-mpv-launch.py`
- Test: 無獨立測試檔（用 `--selftest` 旗標自測 `multiplier_to_profile` / `build_argv`，見 Step 6）

- [ ] **Step 1: 寫 host 完整程式**

```python
#!/usr/bin/env python3
"""Native Messaging host：收 {url, multiplier} → 啟動 mpv + lsfg-vk FG（2x/3x/4x）。"""
import json
import os
import struct
import subprocess
import sys

MPV = "mpv"
MPV_ARGS = ["--vo=gpu", "--gpu-api=vulkan"]
ALLOWED_MULT = (2, 3, 4)


def multiplier_to_profile(mult):
    """multiplier → lsfg-vk profile 名；不支援回 None。"""
    if mult not in ALLOWED_MULT:
        return None
    return f"{mult}x FG / 100%"


def build_argv(profile, url):
    """組 mpv 啟動命令（argv 清單）。"""
    return [MPV, *MPV_ARGS, url]


def read_message():
    """讀 Native Messaging 訊息（4-byte little-endian 長度 + JSON）。"""
    raw = sys.stdin.buffer.read(4)
    if len(raw) < 4:
        return None  # stdin 關閉（Chrome 結束 host）
    (length,) = struct.unpack("<I", raw)
    return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))


def write_message(obj):
    """寫 Native Messaging 回應。"""
    data = json.dumps(obj).encode("utf-8")
    sys.stdout.buffer.write(struct.pack("<I", len(data)))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def launch(profile_name, url):
    env = dict(os.environ, LSFGVK_PROFILE=profile_name)
    subprocess.Popen(build_argv(profile_name, url), env=env,
                     start_new_session=True,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL)
    return True


def main():
    try:
        msg = read_message()
    except Exception as e:
        write_message({"ok": False, "error": f"訊息解析失敗: {e}"})
        return 1
    if msg is None:
        return 0
    url = msg.get("url", "")
    mult = msg.get("multiplier", 0)
    profile = multiplier_to_profile(mult)
    if profile is None:
        write_message({"ok": False, "error": f"倍率 {mult} 不支援（只接受 2/3/4）"})
        return 1
    if not url.startswith("https://www.youtube.com/watch?v="):
        write_message({"ok": False, "error": "不是 YouTube 影片網址"})
        return 1
    try:
        launch(profile, url)
    except FileNotFoundError:
        write_message({"ok": False, "error": "mpv 未安裝"})
        return 1
    write_message({"ok": True})
    return 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        assert multiplier_to_profile(2) == "2x FG / 100%"
        assert multiplier_to_profile(3) == "3x FG / 100%"
        assert multiplier_to_profile(4) == "4x FG / 100%"
        assert multiplier_to_profile(5) is None
        assert build_argv("3x FG / 100%", "https://x") == ["mpv", "--vo=gpu", "--gpu-api=vulkan", "https://x"]
        print("host selftest passed")
        sys.exit(0)
    sys.exit(main())
```

- [ ] **Step 2: 執行檔權限 + 自測**

Run: `chmod +x host/fgvk-mpv-launch.py && host/fgvk-mpv-launch.py --selftest`
Expected: 印出 `host selftest passed`

- [ ] **Step 3: 手動 pipe 測試（模擬 Native Messaging 訊框）**

Run（送 `{"url":"https://www.youtube.com/watch?v=dQw4w9WgXcQ","multiplier":3}`，4-byte 長度前綴）:
```bash
python3 -c 'import json,sys,struct; d=json.dumps({"url":"https://www.youtube.com/watch?v=dQw4w9WgXcQ","multiplier":3}).encode(); sys.stdout.buffer.write(struct.pack("<I",len(d))+d)' | host/fgvk-mpv-launch.py
```
Expected: 回 `{"ok": true}`（且 mpv 視窗跳出播放該影片，代表 LSFGVK_PROFILE="3x FG / 100%" 生效）。若 mpv 沒跳出，檢查 `command -v mpv`。

- [ ] **Step 4: Commit**

```bash
git add youtube-fg-extension/host/fgvk-mpv-launch.py
git commit -m "feat(extension): Python Native Messaging host（mpv + LSFGVK_PROFILE）"
```

---

### Task 3: host manifest

**Files:**
- Create: `youtube-fg-extension/host/com.fgvk.host.json`

- [ ] **Step 1: 寫 manifest（先放佔位 `<EXTENSION_ID>`，Task 5 裝完後回填）**

```json
{
  "name": "com.fgvk.host",
  "description": "Launch MPV with lsfg-vk frame generation",
  "path": "/home/issac/.local/bin/fgvk-mpv-launch.py",
  "type": "stdio",
  "allowed_origins": [
    "chrome-extension://<EXTENSION_ID>/"
  ]
}
```

- [ ] **Step 2: Commit**

```bash
git add youtube-fg-extension/host/com.fgvk.host.json
git commit -m "feat(extension): native messaging host manifest"
```

---

### Task 4: 擴充套件 manifest.json

**Files:**
- Create: `youtube-fg-extension/manifest.json`

- [ ] **Step 1: 寫 MV3 manifest**

```json
{
  "manifest_version": 3,
  "name": "YouTube FG (mpv)",
  "version": "0.1.0",
  "description": "一鍵把 YouTube 影片送到 mpv 補幀（lsfg-vk 2x/3x/4x）",
  "action": {
    "default_popup": "popup.html"
  },
  "permissions": ["tabs", "nativeMessaging"],
  "host_permissions": ["https://www.youtube.com/*"]
}
```

- [ ] **Step 2: Commit**

```bash
git add youtube-fg-extension/manifest.json
git commit -m "feat(extension): MV3 manifest"
```

---

### Task 5: popup（HTML + JS）

**Files:**
- Create: `youtube-fg-extension/popup.html`
- Create: `youtube-fg-extension/popup.js`

- [ ] **Step 1: 寫 popup.html**

```html
<!doctype html>
<html>
<head><meta charset="utf-8"><style>
  body { width: 200px; font-family: sans-serif; padding: 12px; }
  button { display: block; width: 100%; margin: 6px 0; padding: 10px; font-size: 15px; cursor: pointer; }
  #status { margin-top: 8px; font-size: 12px; color: #666; }
</style></head>
<body>
  <button data-mult="2">2x FG</button>
  <button data-mult="3">3x FG</button>
  <button data-mult="4">4x FG</button>
  <div id="status"></div>
  <script src="url-utils.js"></script>
  <script src="popup.js"></script>
</body>
</html>
```

- [ ] **Step 2: 寫 popup.js**

```js
// popup.js
const statusEl = document.getElementById("status");

function setStatus(text) {
  statusEl.textContent = text;
}

async function launch(mult) {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  const url = tab && tab.url;
  if (!isYouTubeWatchUrl(url)) {
    setStatus("此頁不是 YouTube 影片");
    return;
  }
  const profile = multiplierToProfile(mult);
  chrome.runtime.sendNativeMessage("com.fgvk.host", { url, multiplier: mult }, (resp) => {
    if (chrome.runtime.lastError) {
      setStatus("未安裝 host：" + chrome.runtime.lastError.message);
      return;
    }
    if (resp && resp.ok) {
      setStatus(`已啟動（${profile}）`);
    } else {
      setStatus((resp && resp.error) || "啟動失敗");
    }
  });
}

document.querySelectorAll("button[data-mult]").forEach((btn) => {
  btn.addEventListener("click", () => launch(Number(btn.dataset.mult)));
});
```

- [ ] **Step 3: 手動驗證**

在 `chrome://extensions` 開 Developer mode → Load unpacked 選 `youtube-fg-extension/` 目錄。記下**產生的 extension ID**（稍後填進 Task 3 的 host manifest `allowed_origins`）。到任一 YouTube 影片頁點擴充套件圖示 → 點「3x」→ 若顯示「未安裝 host」屬正常（host 還沒裝到系統位）；確認按鈕/logic 沒 JS 錯誤（popup 右鍵 → Inspect → Console）。

- [ ] **Step 4: Commit**

```bash
git add youtube-fg-extension/popup.html youtube-fg-extension/popup.js
git commit -m "feat(extension): popup（2x/3x/4x 按鈕 + 送 Native Messaging）"
```

---

### Task 6: 安裝文件 + lsfg-vk profiles

**Files:**
- Create: `youtube-fg-extension/install.md`
- Modify: `~/.config/lsfg-vk/conf.toml`（本機，非 git）

- [ ] **Step 1: 寫 install.md（一次性安裝完整步驟）**

```markdown
# YouTube FG Extension 安裝

## 1. lsfg-vk profiles（本機 conf.toml）
在 `~/.config/lsfg-vk/conf.toml` 確保有三個 profile（`flow_scale=1.0`、`performance_mode=false`）：

[[profile]]
name = "2x FG / 100%"
multiplier = 2
[[profile]]
name = "3x FG / 100%"
multiplier = 3
[[profile]]
name = "4x FG / 100%"
multiplier = 4

（其餘欄位跟現有 2x profile 一致：`override_present_mode=true`、`pacing_mode="vsync"`、`preserve_swapchain_image_count=false`。）

## 2. 裝 host
- `mkdir -p ~/.local/bin`
- `cp host/fgvk-mpv-launch.py ~/.local/bin/ && chmod +x ~/.local/bin/fgvk-mpv-launch.py`
- `mkdir -p ~/.config/google-chrome/NativeMessagingHosts`
- `cp host/com.fgvk.host.json ~/.config/google-chrome/NativeMessagingHosts/`
- 把 host manifest 的 `<EXTENSION_ID>` 換成步驟 4 產生的 ID，並確認 `path` 是 `~/.local/bin/fgvk-mpv-launch.py` 的**絕對路徑**。

## 3. 載入擴充套件
`chrome://extensions` → Developer mode → Load unpacked → 選 `youtube-fg-extension/`。

## 4. 回填 extension ID
Chrome 會顯示擴充套件的 ID（亂碼字串）。把它填進
`~/.config/google-chrome/NativeMessagingHosts/com.fgvk.host.json` 的
`"allowed_origins": ["chrome-extension://<ID>/"]`，然後重啟 Chrome。

## 5. 驗證
開任一 YouTube 影片 → 點擴充套件圖示 → 按「2x/3x/4x」→ mpv 跳出影片並補幀。
```

- [ ] **Step 2: 實際改 conf.toml（本機，加了 3x profile）**

確保 `~/.config/lsfg-vk/conf.toml` 有 `3x FG / 100%`（multiplier=3）。用備份方式編輯：`cp ~/.config/lsfg-vk/conf.toml ~/.config/lsfg-vk/conf.toml.bak-ext` 再改。**用真影片驗證**：`LSFGVK_PROFILE="3x FG / 100%" mpv --vo=gpu --gpu-api=vulkan "https://www.youtube.com/watch?v=aC0RkHbCM8w"` 應能補幀（沿用先前已確認的 mpv+FG 通）。

- [ ] **Step 3: Commit**

```bash
git add youtube-fg-extension/install.md
git commit -m "docs(extension): 安裝步驟"
```

---

## Self-Review 結果

- **Spec 覆蓋**：目標、元件、資料流、協議、錯誤處理、安裝、非目標 —— 每個都有對應 Task。倍率映射在 `multiplierToProfile`（Task 1）+ `multiplier_to_profile`（Task 2）兩處各做一次（JS 端供 UI、Python 端供 host 最終把關）；兩者值一致（`Nx FG / 100%`）。
- **佔位掃描**：唯一佔位是 host manifest 的 `<EXTENSION_ID>`，屬「載入後回填」的正常流程（Task 5→3 迴填），非未完成。
- **型別一致性**：訊息鍵 `url`/`multiplier` 在 popup.js（送出）、host（`msg.get("url")`/`msg.get("multiplier")`）一致；profile 名 `Nx FG / 100%` 全篇一致。
