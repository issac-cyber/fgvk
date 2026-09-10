# YouTube FG Extension 安裝

## 1. lsfg-vk profiles（本機 conf.toml）
在 `~/.config/lsfg-vk/conf.toml` 確保有三個 profile（`flow_scale=1.0`、`performance_mode=false`）：

```toml
[[profile]]
name = "2x FG / 100%"
multiplier = 2

[[profile]]
name = "3x FG / 100%"
multiplier = 3

[[profile]]
name = "4x FG / 100%"
multiplier = 4
```

（其餘欄位跟現有 2x profile 一致：`override_present_mode=true`、`pacing_mode="vsync"`、`preserve_swapchain_image_count=false`。）

## 2. 裝 host
```bash
mkdir -p ~/.local/bin
cp host/fgvk-mpv-launch.py ~/.local/bin/ && chmod +x ~/.local/bin/fgvk-mpv-launch.py
mkdir -p ~/.config/google-chrome/NativeMessagingHosts
cp host/com.fgvk.host.json ~/.config/google-chrome/NativeMessagingHosts/
```
把 host manifest 的 `<EXTENSION_ID>` 換成步驟 4 產生的 ID，並確認 `path` 是 `fgvk-mpv-launch.py` 的**絕對路徑**。

## 3. 載入擴充套件
`chrome://extensions` → Developer mode → Load unpacked → 選 `youtube-fg-extension/` 目錄。

## 4. 回填 extension ID
Chrome 會顯示擴充套件的 ID（亂碼字串）。把它填進
`~/.config/google-chrome/NativeMessagingHosts/com.fgvk.host.json` 的
`"allowed_origins": ["chrome-extension://<ID>/"]`，然後重啟 Chrome。

## 5. 驗證
開任一 YouTube 影片 → 點擴充套件圖示 → 按「2x/3x/4x」→ mpv 跳出影片並補幀。
