// SprintSplit - configuration loading.
#pragma once

#include <windows.h>

#include <string>

#include "sprint_logic.h"

namespace ss {

struct Config {
    Options      options;
    std::wstring ini_path;
    std::wstring log_path;
    bool         loaded = false;
    bool         log_enabled = true;
    std::string  status;  // human readable summary for the log
};

// Reads <module_dir>\SprintSplit\SprintSplit.ini. Missing keys keep defaults,
// a missing file is not an error (the mod then runs with defaults).
Config LoadConfig(const std::wstring& module_dir);

// Parses "B", "LB+RB", "L3", "none", ... into an XInput button mask.
// Returns false (and leaves *mask untouched) when a token is unknown.
bool ParseButtonList(const std::string& text, std::uint16_t* mask);

}  // namespace ss
