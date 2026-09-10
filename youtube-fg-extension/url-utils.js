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
