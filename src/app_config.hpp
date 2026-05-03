#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app_theme.hpp"
#include "pane_state.hpp"

enum class eSessionMode : std::uint8_t {
    MOCK,
    DAP_LAUNCH,
};

struct SDapLaunchConfig {
    std::string command;
    std::string liblldb_path;
    std::string program;
    std::string working_directory = ".";
    bool        stop_on_entry     = true;
    bool        continue_once     = false;
};

struct SAppConfig {
    eSessionMode             session_mode          = eSessionMode::MOCK;
    eFocusPane               startup_focus         = eFocusPane::MEMORY_VIEW;
    bool                     show_memory_view      = true;
    bool                     show_disassembly_view = false;
    eThemePreset             theme_preset          = eThemePreset::DEFAULT;
    SThemeOverrides          theme_overrides       = {};
    SDapLaunchConfig         dap_launch            = {};
    std::vector<SKeybinding> keybindings           = defaultKeybindings();
};

SAppConfig loadAppConfig(const std::string& config_path);
