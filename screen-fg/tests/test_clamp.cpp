#include "clamp.hpp"
#include "test_framework.hpp"

using namespace screenfg;

static void test_clamp() {
    // 167Hz / 60fps → 2x
    CHECK_EQ(maxMultiplier(167, 60), 2);
    CHECK_EQ(maxMultiplier(144, 60), 2);
    CHECK_EQ(maxMultiplier(120, 60), 2);
    CHECK_EQ(maxMultiplier(119, 60), 1);
    CHECK_EQ(maxMultiplier(60, 60), 1);
    CHECK_EQ(maxMultiplier(240, 60), 4);
    CHECK_EQ(maxMultiplier(144, 30), 4);
    // 邊界
    CHECK_EQ(maxMultiplier(0, 60), 1);
    CHECK_EQ(maxMultiplier(167, 0), 1);
    // 捕捉上限版本
    CHECK_EQ(maxMultiplierForCapture(167), 2);
    CHECK_EQ(maxMultiplierForCapture(144), 2);
    CHECK_EQ(maxMultiplierForCapture(120), 2);
    CHECK_EQ(maxMultiplierForCapture(119), 1);
    CHECK_EQ(maxMultiplierForCapture(240), 4);
}

static Registrar reg("clamp", test_clamp);
