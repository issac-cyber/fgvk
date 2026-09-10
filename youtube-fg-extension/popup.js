// popup.js
const statusEl = document.getElementById("status");

function setStatus(text) {
  statusEl.textContent = text;
}

async function launch(mult) {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  const url = tab && tab.url;
  if (!isVideoUrl(url)) {
    setStatus("此頁非 http(s) 網頁");
    return;
  }
  // 靜音來源 tab（音頻改由 mpv 提供 → 單一播放器，音畫同步最佳）
  if (tab && tab.id != null) {
    try { await chrome.tabs.update(tab.id, { muted: true }); } catch (e) {}
  }
  const profile = multiplierToProfile(mult);
  chrome.runtime.sendNativeMessage("com.fgvk.host", { url, multiplier: mult }, (resp) => {
    if (chrome.runtime.lastError) {
      setStatus("未安裝 host：" + chrome.runtime.lastError.message);
      return;
    }
    if (resp && resp.ok) {
      setStatus(`已啟動（${profile}）· 來源 tab 已靜音`);
    } else {
      setStatus((resp && resp.error) || "啟動失敗");
    }
  });
}

document.querySelectorAll("button[data-mult]").forEach((btn) => {
  btn.addEventListener("click", () => launch(Number(btn.dataset.mult)));
});
