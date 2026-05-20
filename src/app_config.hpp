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
    std::int64_t          line                 = 0;
    bool                  enabled              = true;
    bool                  adapter_status_known = false;
    bool                  adapter_verified     = false;
    std::int64_t          adapter_line         = 0;
    std::string           adapter_message;
};

struct SLaunchProfileConfig {
    std::string                                         name;
    std::optional<std::string>                          command;
    std::optional<std::string>                          liblldb_path;
    std::optional<std::string>                          program;
    std::optional<std::vector<std::string>>             arguments;
    std::optional<std::string>                          working_directory;
    std::optional<bool>                                 stop_on_entry;
    std::optional<bool>                                 continue_once;
    std::optional<std::vector<std::string>>             watches;
    std::optional<std::vector<SSourceBreakpointConfig>> breakpoints;
};

struct SCodeLldbAutoDetectConfig {
    bool                               enabled = true;
    std::vector<std::filesystem::path> candidate_roots;
};

struct SAppConfig {
    eSessionMode                         session_mode          = eSessionMode::DAP_LAUNCH;
    eFocusPane                           startup_focus         = eFocusPane::MEMORY_VIEW;
    bool                                 show_memory_view      = true;
    bool                                 show_disassembly_view = false;
    eThemePreset                         theme_preset          = eThemePreset::DEFAULT;
    SThemeOverrides                      theme_overrides       = {};
    SDapLaunchConfig                     dap_launch            = {};
    std::string                          active_profile        = {};
    std::vector<SLaunchProfileConfig>    launch_profiles       = {};
    std::vector<SSourceBreakpointConfig> breakpoints           = {};
    std::vector<std::string>             watches               = {};
    SCodeLldbAutoDetectConfig            codelldb_auto_detect  = {};
    std::vector<SKeybinding>             keybindings           = defaultKeybindings();
};

struct SAppConfigLoadResult {
    SAppConfig               config;
    std::vector<std::string> diagnostics;
};

struct SAppConfigInitResult {
    bool                  ok      = false;
    bool                  created = false;
    std::filesystem::path path;
    std::string           message;
};

SAppConfig                             loadAppConfig(const std::string& config_path);
SAppConfigLoadResult                   loadAppConfigWithDiagnostics(const std::string& config_path);
SAppConfigLoadResult                   loadAppConfigWithBaseConfig(const SAppConfig& base_config, const std::string& config_path);
SAppConfigLoadResult                   loadAppConfigForCliWithDiagnostics(const std::string& config_path, bool config_path_explicit);
std::vector<std::filesystem::path>     defaultAppConfigSearchPaths();
std::optional<std::filesystem::path>   findDefaultAppConfigPath();
std::filesystem::path                  defaultUserAppConfigPath();
std::string                            defaultAppConfigTemplate();
SAppConfigInitResult                   initializeUserAppConfig(bool force);
bool                                   applyLaunchProfile(SAppConfig& config, const std::string& profile_name);
std::optional<SSourceBreakpointConfig> parseSourceBreakpointConfig(std::string value);
