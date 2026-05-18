#pragma once

#include <filesystem>
#include <optional>

#include "app_config.hpp"

struct SCliOptions {
    bool                                 show_help   = false;
    std::filesystem::path                config_path = "roundtable.toml";
    std::optional<std::filesystem::path> launch_program;
};

SCliOptions parseCliOptions(int argc, char** argv);
void        applyCliOverrides(const SCliOptions& cli_options, SAppConfig& app_config);
