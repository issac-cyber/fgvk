#include "reexec.hpp"
#include "test_framework.hpp"

#include <optional>
#include <string>

using namespace screenfg;

static void test_reexec() {
    // PassthroughBoot：plain + paused + 不設 HUD env（nullopt）
    auto b = resolveReexec(false, ReexecAction::PassthroughBoot, std::nullopt);
    CHECK_EQ(b.targetName, std::string("screen-fg-plain"));
    CHECK_EQ(b.stateEnv, std::string("paused"));
    CHECK_EQ(b.hudEnv, std::string(""));

    // Pause：plain + paused + HUD env 跟隨
    auto p = resolveReexec(false, ReexecAction::Pause, true);
    CHECK_EQ(p.targetName, std::string("screen-fg-plain"));
    CHECK_EQ(p.stateEnv, std::string("paused"));
    CHECK_EQ(p.hudEnv, std::string("1"));
    auto p0 = resolveReexec(false, ReexecAction::Pause, false);
    CHECK_EQ(p0.hudEnv, std::string("0"));

    // Resume：screen-fg + normal + HUD env
    auto r = resolveReexec(true, ReexecAction::Resume, true);
    CHECK_EQ(r.targetName, std::string("screen-fg"));
    CHECK_EQ(r.stateEnv, std::string("normal"));
    CHECK_EQ(r.hudEnv, std::string("1"));

    // Toggle：paused → screen-fg/normal；normal → plain/paused
    auto t1 = resolveReexec(true, ReexecAction::Toggle, false);
    CHECK_EQ(t1.targetName, std::string("screen-fg"));
    CHECK_EQ(t1.stateEnv, std::string("normal"));
    auto t0 = resolveReexec(false, ReexecAction::Toggle, false);
    CHECK_EQ(t0.targetName, std::string("screen-fg-plain"));
    CHECK_EQ(t0.stateEnv, std::string("paused"));
}

static Registrar reg("reexec", test_reexec);
