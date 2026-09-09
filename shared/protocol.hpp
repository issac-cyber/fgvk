// shared/protocol.hpp — stdio 控制協議，screen-fg 與 screen-fg-gui 共同使用。
// 純（無 I/O）：emit 回傳整行 bytes（含 '\n'，transport 負責寫）；
// parse 回傳 typed Message。兩端編譯同一份 header，詞彙/結構/檢查不可能漂移。
//
// 「兩端一致」檢查：message 的必需欄位缺失 / 格式錯誤 → parse 回 nullopt
// （明確報錯，不默默給 default）。kVersion 為共編譯的合約版本。
#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <variant>

namespace screenfg::protocol {

// 不兼容變更時遞增（兩端編同一份 header，天然一致）。
inline constexpr int kVersion = 1;

// 狀態詞彙（status.state / re-exec 的 SCREENFG_STATE env）。
namespace State {
inline constexpr const char* Running = "running";
inline constexpr const char* Paused = "paused";
inline constexpr const char* Exiting = "exiting";
} // namespace State

// 命令詞彙（stdin，line-oriented）。
namespace Cmd {
inline constexpr const char* Quit = "quit";
inline constexpr const char* Pause = "pause";
inline constexpr const char* Resume = "resume";
inline constexpr const char* Hud = "hud";
} // namespace Cmd

struct Status {
    int fps = 0;
    unsigned int mult = 0;
    bool layer = false;
    std::string state;
};
struct Exit {
    int code = 0;
};

// typed message：Status | Exit。
using Message = std::variant<Status, Exit>;

// ---- 純 emit（回傳整行，含尾 '\n'；transport 負責寫）----

inline std::string emitStatus(int fps, unsigned int mult, bool layer, const std::string& state) {
    return "{\"type\":\"status\",\"fps\":" + std::to_string(fps) +
           ",\"mult\":" + std::to_string(mult) +
           ",\"layer\":" + std::string(layer ? "true" : "false") +
           ",\"state\":\"" + state + "\"}\n";
}

inline std::string emitExit(int code) {
    return "{\"type\":\"exit\",\"code\":" + std::to_string(code) + "}\n";
}

// ---- 純 parse（欄位存在性檢查；缺 / 壞 → nullopt，非默默 default）----

// flat key 抽取：找 "key"，取其后第一個 ':' 之後、到 ',' / '"' / '}' / 空白為止的 token；
// key 不存在 → nullopt。
inline std::optional<std::string> rawField(const std::string& line, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t pos = line.find(k);
    if (pos == std::string::npos)
        return std::nullopt;
    const size_t colon = line.find(':', pos + k.size());
    if (colon == std::string::npos)
        return std::nullopt;
    size_t start = colon + 1;
    while (start < line.size() && line[start] == ' ')
        ++start;
    size_t end = start;
    while (end < line.size() && line[end] != ',' && line[end] != '"' && line[end] != '}' && line[end] != ' ')
        ++end;
    if (end == start)
        return std::nullopt;
    return line.substr(start, end - start);
}

inline std::optional<int> intField(const std::string& line, const char* key) {
    auto v = rawField(line, key);
    if (!v)
        return std::nullopt;
    size_t i = 0;
    if (!v->empty() && (*v)[0] == '-')
        i = 1;
    if (i == v->size())
        return std::nullopt;
    for (size_t j = i; j < v->size(); ++j)
        if (!std::isdigit(static_cast<unsigned char>((*v)[j])))
            return std::nullopt;
    return std::stoi(*v);
}

inline std::optional<bool> boolField(const std::string& line, const char* key) {
    auto v = rawField(line, key);
    if (!v)
        return std::nullopt;
    if (*v == "true")
        return true;
    if (*v == "false")
        return false;
    return std::nullopt;
}

// 字串值（引號內）抽取；key 不存在或無引號值 → nullopt。
inline std::optional<std::string> stringField(const std::string& line, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t pos = line.find(k);
    if (pos == std::string::npos)
        return std::nullopt;
    const size_t colon = line.find(':', pos + k.size());
    if (colon == std::string::npos)
        return std::nullopt;
    const size_t q1 = line.find('"', colon + 1);
    if (q1 == std::string::npos)
        return std::nullopt;
    const size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return std::nullopt;
    return line.substr(q1 + 1, q2 - q1 - 1);
}

// 解析一列協議行。nullopt：type 非 status/exit，或任一必需欄位缺失/格式錯誤。
inline std::optional<Message> parse(const std::string& line) {
    const auto type = stringField(line, "type");
    if (!type)
        return std::nullopt;
    if (*type == "status") {
        const auto fps = intField(line, "fps");
        const auto mult = intField(line, "mult");
        const auto layer = boolField(line, "layer");
        const auto state = stringField(line, "state");
        if (!fps || !mult || !layer || !state)
            return std::nullopt;
        Status s;
        s.fps = *fps;
        s.mult = static_cast<unsigned int>(*mult);
        s.layer = *layer;
        s.state = *state;
        return Message{s};
    }
    if (*type == "exit") {
        const auto code = intField(line, "code");
        if (!code)
            return std::nullopt;
        Exit e;
        e.code = *code;
        return Message{e};
    }
    return std::nullopt;
}

} // namespace screenfg::protocol
