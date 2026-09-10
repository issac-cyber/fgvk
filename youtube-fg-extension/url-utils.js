// url-utils.js — 純函數，瀏覽器（全域）與 Node（CommonJS）皆可用
// 放寬：任何 http(s) 網頁皆可（mpv + yt-dlp 自動解析串流站／直接媒體網址；DRM 站會失敗）
function isVideoUrl(url) {
  if (typeof url !== "string" || url === "") return false;
  return url.startsWith("http://") || url.startsWith("https://");
}

function multiplierToProfile(mult) {
  const n = Number(mult);
  if (![2, 3, 4, 10].includes(n)) return null;
  return `${n}x FG / 100%`;
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = { isVideoUrl, multiplierToProfile };
}
