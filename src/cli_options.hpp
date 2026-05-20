#pragma once

#include <filesystem>
#include <optional>

#include "app_config.hpp"

struct SCliOptions {
    bool                                 show_help            = false;
    bool                                 show_version         = false;
    bool                                 init_config          = false;
    bool                                 force_init_config    = false;
    bool                                 force_mock           = false;
    bool                                 config_path_explicit = false;
    std::filesystem::path                config_path          = "roundtable.toml";
    std::optional<std::string>           profile;
    std::optional<std::filesystem::path> launch_program;
};

SCliOptions parseCliOptions(int argc, char** argv);
void        applyCliOverrides(const SCliOptions& cli_options, SAppConfig& app_config);
