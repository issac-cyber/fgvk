// test/url-utils.test.cjs
const assert = require("assert");
const { isYouTubeWatchUrl, multiplierToProfile } = require("../url-utils.js");

// isYouTubeWatchUrl
assert.strictEqual(isYouTubeWatchUrl("https://www.youtube.com/watch?v=abc"), true);
assert.strictEqual(isYouTubeWatchUrl("https://youtube.com/watch?v=abc"), true);
assert.strictEqual(isYouTubeWatchUrl("https://www.youtube.com/watch?v=abc&t=10"), true);
assert.strictEqual(isYouTubeWatchUrl("https://www.youtube.com/"), false);
assert.strictEqual(isYouTubeWatchUrl("https://www.youtube.com/results?search_query=x"), false);
assert.strictEqual(isYouTubeWatchUrl("https://example.com/watch?v=abc"), false);
assert.strictEqual(isYouTubeWatchUrl(""), false);
assert.strictEqual(isYouTubeWatchUrl(null), false);
assert.strictEqual(isYouTubeWatchUrl("not a url"), false);

// multiplierToProfile
assert.strictEqual(multiplierToProfile(2), "2x FG / 100%");
assert.strictEqual(multiplierToProfile(3), "3x FG / 100%");
assert.strictEqual(multiplierToProfile(4), "4x FG / 100%");
assert.strictEqual(multiplierToProfile(10), "10x FG / 100%");
assert.strictEqual(multiplierToProfile(5), null);
assert.strictEqual(multiplierToProfile("x"), null);

console.log("url-utils tests passed");
