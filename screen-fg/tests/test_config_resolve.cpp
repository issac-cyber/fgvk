#include "config.hpp"
#include "test_framework.hpp"

using namespace screenfg;

static void test_config_resolve() {
    // 純 toml（無 env）
    auto r0 = resolveConfig("display = 2\nhud = false\n", {});
    CHECK_EQ(r0.config.displayIndex, 2);
    CHECK(!r0.config.hud);
    CHECK(!r0.paused);

    // env 覆蓋 toml
    EnvMap env;
    env["SCREENFG_DISPLAY"] = "5";
    env["SCREENFG_HUD"] = "1";
    auto r1 = resolveConfig("display = 2\nhud = false\n", env);
    CHECK_EQ(r1.config.displayIndex, 5);
    CHECK(r1.config.hud);

    // STATE 決定 paused
    EnvMap envPaused;
    envPaused["SCREENFG_STATE"] = "paused";
    CHECK(resolveConfig("", envPaused).paused);
    EnvMap envNormal;
    envNormal["SCREENFG_STATE"] = "normal";
    CHECK(!resolveConfig("", envNormal).paused);

    // cardIndexFor：本機 identity
    CHECK_EQ(cardIndexFor(0), 0);
    CHECK_EQ(cardIndexFor(1), 1);
}

static Registrar reg("config_resolve", test_config_resolve);
