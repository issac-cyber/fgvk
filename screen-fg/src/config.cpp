#include "config.hpp"

#include "reexec.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace screenfg {

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

Config parseConfig(const std::string& text, Config def) {
    Config c = def;
    std::istringstream ss(text);
    std::string line;
    int lineno = 0;
    while (std::getline(ss, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("line " + std::to_string(lineno) + ": expected 'key = value'");
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (val.empty())
            throw std::runtime_error("line " + std::to_string(lineno) + ": empty value");
        bool quoted = val.size() >= 2 && val.front() == '"' && val.back() == '"';
        try {
            if (key == "dedup_threshold") {
                c.dedupThreshold = std::stof(val);
            } else if (key == "hud") {
                if (val == "true")
                    c.hud = true;
                else if (val == "false")
                    c.hud = false;
                else
                    throw std::invalid_argument("bool");
            } else if (key == "profile") {
                if (!quoted)
                    throw std::invalid_argument("quoted");
                c.profile = val.substr(1, val.size() - 2);
            } else if (key == "gpu") {
                if (!quoted)
                    throw std::invalid_argument("quoted");
                c.gpu = val.substr(1, val.size() - 2);
            } else if (key == "display") {
                c.displayIndex = std::stoi(val);
            } else if (key == "content_fps_cap") {
                c.contentFpsCap = static_cast<uint32_t>(std::stoul(val));
            } else if (key == "capture_mode") {
                std::string v = val;
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                    v = v.substr(1, v.size() - 2); // 容錯：引號/未引號皆可
                if (v != "window" && v != "monitor")
                    throw std::invalid_argument("window|monitor");
                c.captureMode = v;
            }
            // unknown keys: ignored (forward compatibility)
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("line " + std::to_string(lineno) + ": bad value '" + val + "' for '" + key + "'");
        }
    }
    return c;
}

ResolvedConfig resolveConfig(const std::string& tomlText, const EnvMap& envMap) {
    ResolvedConfig rc;
    rc.config = parseConfig(tomlText, Config{});
    // env 層（覆蓋 toml 層）
    if (auto it = envMap.find("SCREENFG_HUD"); it != envMap.end() && !it->second.empty())
        rc.config.hud = (it->second[0] == '1');
    if (auto it = envMap.find("SCREENFG_DISPLAY"); it != envMap.end() && !it->second.empty())
        rc.config.displayIndex = std::atoi(it->second.c_str());
    if (auto it = envMap.find("SCREENFG_STATE"); it != envMap.end())
        rc.paused = (it->second == ReexecEnv::Paused);
    return rc;
}

int cardIndexFor(int displayIndex) {
    // 本機：2 張 R9700 各驅一塊顯示，display N 在 card N（identity）。
    return displayIndex;
}

ResolvedConfig loadConfig(const std::string& path) {
    std::string text;
    std::ifstream f(path);
    if (f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    EnvMap env;
    const char* keys[] = {"SCREENFG_HUD", "SCREENFG_DISPLAY", "SCREENFG_STATE"};
    for (const char* k : keys)
        if (const char* v = std::getenv(k))
            env[k] = v;
    return resolveConfig(text, env);
}

} // namespace screenfg
