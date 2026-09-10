// test/url-utils.test.cjs
const assert = require("assert");
const { isVideoUrl, multiplierToProfile } = require("../url-utils.js");

// isVideoUrl（放寬：任何 http(s) 網頁）
assert.strictEqual(isVideoUrl("https://www.youtube.com/watch?v=abc"), true);
assert.strictEqual(isVideoUrl("https://www.bilibili.com/video/BV1xx411c7mD"), true);
assert.strictEqual(isVideoUrl("https://example.com/video.mp4"), true);
assert.strictEqual(isVideoUrl("http://example.com/stream.m3u8"), true);
assert.strictEqual(isVideoUrl("https://vimeo.com/123456"), true);
assert.strictEqual(isVideoUrl("chrome://extensions"), false);
assert.strictEqual(isVideoUrl("file:///tmp/a.mp4"), false);
assert.strictEqual(isVideoUrl(""), false);
assert.strictEqual(isVideoUrl(null), false);
assert.strictEqual(isVideoUrl("not a url"), false);

// multiplierToProfile
assert.strictEqual(multiplierToProfile(2), "2x FG / 100%");
assert.strictEqual(multiplierToProfile(3), "3x FG / 100%");
assert.strictEqual(multiplierToProfile(4), "4x FG / 100%");
assert.strictEqual(multiplierToProfile(10), "10x FG / 100%");
assert.strictEqual(multiplierToProfile(5), null);
assert.strictEqual(multiplierToProfile("x"), null);

console.log("url-utils tests passed");
