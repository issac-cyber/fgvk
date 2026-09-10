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
