#pragma once

#include <string>
#include <vector>

#include "pane_state.hpp"

struct SAppConfig {
    bool                     show_memory_view       = true;
    bool                     show_disassembly_view  = false;
    std::vector<SKeybinding> keybindings           = defaultKeybindings();
};

SAppConfig loadAppConfig(const std::string& config_path);
