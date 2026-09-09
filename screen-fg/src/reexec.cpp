#include "reexec.hpp"

#include <cstring>
#include <cstdlib>
#include <iostream>

#include <unistd.h>

namespace screenfg {

// ---------- 純 resolver ----------

ReexecPlan resolveReexec(bool currentlyPaused, ReexecAction action, std::optional<bool> hud) {
    // HUD env：hud 有值才設（"1"/"0"）；nullopt（boot）不設（""）。
    auto hudEnv = [hud]() -> std::string {
        if (!hud)
            return std::string();
        return *hud ? "1" : "0";
    };
    ReexecPlan p;
    switch (action) {
        case ReexecAction::PassthroughBoot:
            p.targetName = "screen-fg-plain";
            p.stateEnv = ReexecEnv::Paused;
            p.hudEnv = hudEnv();
            break;
        case ReexecAction::Pause:
            p.targetName = "screen-fg-plain";
            p.stateEnv = ReexecEnv::Paused;
            p.hudEnv = hudEnv();
            break;
        case ReexecAction::Resume:
            p.targetName = "screen-fg";
            p.stateEnv = ReexecEnv::Normal;
            p.hudEnv = hudEnv();
            break;
        case ReexecAction::Toggle:
            if (currentlyPaused) {
                p.targetName = "screen-fg";
                p.stateEnv = ReexecEnv::Normal;
            } else {
                p.targetName = "screen-fg-plain";
                p.stateEnv = ReexecEnv::Paused;
            }
            p.hudEnv = hudEnv();
            break;
    }
    return p;
}

// ---------- 注入 self-path 的 adapter ----------

std::string selfPath() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return {};
    buf[n] = 0;
    return buf;
}

void ensurePlainCopy(const std::string& selfP) {
    auto pos = selfP.rfind('/');
    if (pos == std::string::npos)
        return;
    std::string base = selfP.substr(pos + 1);
    if (base == "screen-fg-plain")
        return;
    std::string plainP = selfP.substr(0, pos + 1) + "screen-fg-plain";
    // 每次都重新複製：build 更新後舊 plain copy 會過期（曾導致 pause re-exec 跑到舊 binary）
    int rc = system(("cp -f '" + selfP + "' '" + plainP + "' 2>/dev/null").c_str());
    (void) rc;
}

void reexec(const ReexecPlan& plan, int argc, char** argv, const std::string& selfP) {
    auto pos = selfP.rfind('/');
    std::string target = (pos == std::string::npos ? std::string() : selfP.substr(0, pos + 1)) + plan.targetName;
    char* argp[64];
    int ni = 0;
    argp[ni++] = const_cast<char*>(target.c_str());
    for (int i = 1; i < argc && ni < 63; ++i) {
        if (strcmp(argv[i], "--passthrough") == 0)
            continue; // 狀態改由 env 帶（避免無限 re-exec）
        argp[ni++] = argv[i];
    }
    argp[ni] = nullptr;
    setenv("SCREENFG_STATE", plan.stateEnv.c_str(), 1);
    if (!plan.hudEnv.empty())
        setenv("SCREENFG_HUD", plan.hudEnv.c_str(), 1); // 保留運行期 HUD 狀態跨 re-exec
    execv(target.c_str(), argp);
    std::cerr << "[screen-fg] re-exec 失敗：" << target << "\n";
    std::exit(1);
}

} // namespace screenfg
