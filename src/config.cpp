// SprintSplit - configuration loading (plain Win32, no CRT locale surprises).
#include "config.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace ss {
namespace {

std::string Trim(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end) {
        const char c = s[begin];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++begin;
    }
    while (end > begin) {
        const char c = s[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

void StripComment(std::string* line) {
    for (std::size_t i = 0; i < line->size(); ++i) {
        const char c = (*line)[i];
        if (c != ';' && c != '#') continue;
        if (i == 0 || (*line)[i - 1] == ' ' || (*line)[i - 1] == '\t') {
            line->erase(i);
            return;
        }
    }
}

bool ReadFileBytes(const std::wstring& path, std::string* out) {
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 || size.QuadPart > (1 << 20)) {
        CloseHandle(handle);
        return false;
    }

    out->assign(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = out->empty()
                        ? TRUE
                        : ReadFile(handle, &(*out)[0], static_cast<DWORD>(out->size()),
                                   &read, nullptr);
    CloseHandle(handle);
    if (!ok) return false;
    out->resize(read);
    return true;
}

class Ini {
public:
    bool Load(const std::wstring& path) {
        std::string data;
        if (!ReadFileBytes(path, &data)) return false;
        if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
            static_cast<unsigned char>(data[1]) == 0xBB &&
            static_cast<unsigned char>(data[2]) == 0xBF) {
            data.erase(0, 3);
        }

        std::string section;
        std::size_t pos = 0;
        while (pos <= data.size()) {
            std::size_t line_end = data.find('\n', pos);
            if (line_end == std::string::npos) line_end = data.size();
            std::string line = Trim(data.substr(pos, line_end - pos));
            pos = line_end + 1;

            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line.front() == '[') {
                const std::size_t close = line.find(']');
                if (close != std::string::npos) {
                    section = Lower(Trim(line.substr(1, close - 1)));
                }
                continue;
            }

            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = Lower(Trim(line.substr(0, eq)));
            std::string value = Trim(line.substr(eq + 1));
            StripComment(&value);
            value = Trim(value);
            if (key.empty()) continue;

            if (!section.empty()) key = section + "." + key;
            values_[key] = value;
        }
        return true;
    }

    std::string Get(const std::string& key, const std::string& fallback) const {
        const auto it = values_.find(key);
        return it == values_.end() ? fallback : it->second;
    }

private:
    std::map<std::string, std::string> values_;
};

bool ToBool(const std::string& text, bool fallback) {
    const std::string v = Lower(Trim(text));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

int ToInt(const std::string& text, int fallback) {
    const std::string v = Trim(text);
    if (v.empty()) return fallback;
    char* end = nullptr;
    const long long parsed = std::strtoll(v.c_str(), &end, 10);
    if (end == v.c_str()) return fallback;
    return static_cast<int>(parsed);
}

float ToFloat(const std::string& text, float fallback) {
    const std::string v = Trim(text);
    if (v.empty()) return fallback;
    char* end = nullptr;
    const double parsed = std::strtod(v.c_str(), &end);
    if (end == v.c_str()) return fallback;
    return static_cast<float>(parsed);
}

int ClampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

float ClampFloat(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

struct ButtonName {
    const char*  name;
    std::uint16_t mask;
};

const ButtonName kButtons[] = {
    {"a", kA},              {"b", kB},              {"x", kX},
    {"y", kY},              {"lb", kLeftShoulder},  {"l1", kLeftShoulder},
    {"rb", kRightShoulder}, {"r1", kRightShoulder}, {"ls", kLeftThumb},
    {"l3", kLeftThumb},     {"lstick", kLeftThumb}, {"leftstick", kLeftThumb},
    {"rs", kRightThumb},    {"r3", kRightThumb},    {"rstick", kRightThumb},
    {"rightstick", kRightThumb},
    {"up", kUp},            {"down", kDown},        {"left", kLeft},
    {"right", kRight},      {"dpadup", kUp},        {"dpaddown", kDown},
    {"dpadleft", kLeft},    {"dpadright", kRight},
    {"start", kStart},      {"menu", kStart},       {"back", kBack},
    {"view", kBack},        {"select", kBack},      {"guide", kGuide},
    {"xbox", kGuide},
};

}  // namespace

bool ParseButtonList(const std::string& text, std::uint16_t* mask) {
    std::uint16_t result = 0;
    std::string token;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '+') {
            const std::string name = Lower(Trim(token));
            token.clear();
            if (name.empty()) continue;
            if (name == "none" || name == "off") continue;

            bool found = false;
            for (const ButtonName& entry : kButtons) {
                if (name == entry.name) {
                    result = static_cast<std::uint16_t>(result | entry.mask);
                    found = true;
                    break;
                }
            }
            if (!found) return false;
            continue;
        }
        token.push_back(text[i]);
    }
    *mask = result;
    return true;
}

Config LoadConfig(const std::wstring& module_dir) {
    Config cfg;
    cfg.ini_path = module_dir + L"\\SprintSplit\\SprintSplit.ini";
    cfg.log_path = module_dir + L"\\SprintSplit\\SprintSplit.log";

    Ini ini;
    if (!ini.Load(cfg.ini_path)) {
        cfg.loaded = false;
        cfg.status = "SprintSplit.ini not found, using built-in defaults";
        return cfg;
    }
    cfg.loaded = true;

    Options& o = cfg.options;
    o.enabled = ToBool(ini.Get("general.enabled", "true"), true);
    o.user_index = ClampInt(ToInt(ini.Get("general.userindex", "-1"), -1), -1, 3);
    cfg.log_enabled = ToBool(ini.Get("general.log", "true"), true);

    std::uint16_t sprint = o.sprint_mask;
    if (!ParseButtonList(ini.Get("bindings.sprintbutton", "L3"), &sprint)) {
        cfg.status += "unknown SprintButton, kept L3; ";
    }
    if (sprint == kB) {
        sprint = kLeftThumb;
        cfg.status += "SprintButton=B is not usable, kept L3; ";
    }
    o.sprint_mask = sprint;

    o.deadzone = ClampFloat(ToFloat(ini.Get("bindings.deadzone", "0.25"), 0.25f), 0.05f, 0.95f);
    o.tap_ms = ClampInt(ToInt(ini.Get("timing.tapms", "30"), 30), 16, 400);
    o.pre_gap_ms = ClampInt(ToInt(ini.Get("timing.pregapms", "30"), 30), 0, 400);
    o.post_gap_ms = ClampInt(ToInt(ini.Get("timing.postgapms", "30"), 30), 0, 400);
    o.sprint_release_ms =
        ClampInt(ToInt(ini.Get("timing.sprintreleasems", "150"), 150), 0, 2000);
    o.resume_sprint = ToBool(ini.Get("behavior.resumesprintafterroll", "true"), true);
    o.suppress_sprint_btn = ToBool(ini.Get("behavior.suppresssprintbutton", "true"), true);

    return cfg;
}

}  // namespace ss
