#pragma once

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app_theme.hpp"
#include "pane_state.hpp"

enum class eSessionMode : std::uint8_t {
    MOCK,
    DAP_LAUNCH,
};

struct SDapLaunchConfig {
    std::string              command;
    std::string              liblldb_path;
    std::string              program;
    std::vector<std::string> arguments;
    std::string              working_directory = ".";
    bool                     stop_on_entry     = true;
    bool                     continue_once     = false;
};

struct SSourceBreakpointConfig {
    std::filesystem::path source_path;
    std::int64_t          line = 0;
};

struct SCodeLldbAutoDetectConfig {
    bool                               enabled = true;
    std::vector<std::filesystem::path> candidate_roots;
};

struct SAppConfig {
    eSessionMode                         session_mode          = eSessionMode::MOCK;
    eFocusPane                           startup_focus         = eFocusPane::MEMORY_VIEW;
    bool                                 show_memory_view      = true;
    bool                                 show_disassembly_view = false;
    eThemePreset                         theme_preset          = eThemePreset::DEFAULT;
    SThemeOverrides                      theme_overrides       = {};
    SDapLaunchConfig                     dap_launch            = {};
    std::vector<SSourceBreakpointConfig> breakpoints           = {};
    std::vector<std::string>             watches               = {};
    SCodeLldbAutoDetectConfig            codelldb_auto_detect  = {};
    std::vector<SKeybinding>             keybindings           = defaultKeybindings();
};

SAppConfig                             loadAppConfig(const std::string& config_path);
std::optional<SSourceBreakpointConfig> parseSourceBreakpointConfig(std::string value);
