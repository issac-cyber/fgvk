#include "config.hpp"
#include "test_framework.hpp"

#include <stdexcept>
#include <string>

using namespace screenfg;

static void test_config() {
    Config d;
    d.dedupThreshold = 1.5f;
    d.hud = false;
    d.displayIndex = 3;

    // 基本 key 覆蓋 + 預設保留
    Config c = parseConfig(
        "dedup_threshold = 7.25\n"
        "hud = true\n"
        "display = 1\n",
        d);
    CHECK_EQ(c.dedupThreshold, 7.25f);
    CHECK(c.hud);
    CHECK_EQ(c.displayIndex, 1);

    // 引號字串（profile / gpu）
    Config c2 = parseConfig(
        "profile = \"4x FG / 85% [Performance]\"\n"
        "gpu = \"0000:01:00.0\"\n",
        d);
    CHECK_EQ(c2.profile, std::string("4x FG / 85% [Performance]"));
    CHECK_EQ(c2.gpu, std::string("0000:01:00.0"));

    // 未給的 key 保留預設
    Config c3 = parseConfig("", d);
    CHECK_EQ(c3.dedupThreshold, 1.5f);
    CHECK(!c3.hud);
    CHECK_EQ(c3.displayIndex, 3);

    // 註解 + 空行
    Config c4 = parseConfig(
        "# comment\n"
        "\n"
        "dedup_threshold = 2.5\n",
        d);
    CHECK_EQ(c4.dedupThreshold, 2.5f);

    // 無 '=' 的線 → throw
    bool threw = false;
    try {
        parseConfig("this is not toml\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // 空 value → throw
    threw = false;
    try {
        parseConfig("hud =\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // 壞 bool → throw
    threw = false;
    try {
        parseConfig("hud = maybe\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // profile 未加引號 → throw
    threw = false;
    try {
        parseConfig("profile = 2x FG / 100%\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // content_fps_cap
    Config c5 = parseConfig("content_fps_cap = 30\n", d);
    CHECK_EQ(c5.contentFpsCap, 30u);
}

static Registrar reg("config", test_config);
