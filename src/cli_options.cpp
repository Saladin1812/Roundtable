#include "cli_options.hpp"

#include <string_view>

SCliOptions parseCliOptions(int argc, char** argv) {
    SCliOptions options = {};

    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        const std::string argument = argv[argument_index];
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            continue;
        }

        if (argument == "--version" || argument == "-V") {
            options.show_version = true;
            continue;
        }

        if (argument == "--init-config") {
            options.init_config = true;
            continue;
        }

        if (argument == "--force") {
            options.force_init_config = true;
            continue;
        }

        if (argument == "--config" || argument == "-c") {
            if (argument_index + 1 < argc) {
                ++argument_index;
                options.config_path          = std::filesystem::path(argv[argument_index]);
                options.config_path_explicit = true;
            }
            continue;
        }

        constexpr std::string_view config_prefix = "--config=";
        if (argument.starts_with(config_prefix)) {
            options.config_path          = std::filesystem::path(argument.substr(config_prefix.size()));
            options.config_path_explicit = true;
            continue;
        }

        if (argument == "--profile" || argument == "-p") {
            if (argument_index + 1 < argc) {
                ++argument_index;
                options.profile = argv[argument_index];
            }
            continue;
        }

        constexpr std::string_view profile_prefix = "--profile=";
        if (argument.starts_with(profile_prefix)) {
            options.profile = argument.substr(profile_prefix.size());
            continue;
        }

        if (!options.launch_program.has_value()) {
            options.launch_program = std::filesystem::path(argument);
        }
    }

    return options;
}

void applyCliOverrides(const SCliOptions& cli_options, SAppConfig& app_config) {
    if (cli_options.profile.has_value()) {
        app_config.active_profile = cli_options.profile.value();
        applyLaunchProfile(app_config, cli_options.profile.value());
    }

    if (!cli_options.launch_program.has_value()) {
        return;
    }

    const auto program_path = std::filesystem::absolute(cli_options.launch_program.value());

    app_config.session_mode                 = eSessionMode::DAP_LAUNCH;
    app_config.dap_launch.program           = program_path.string();
    app_config.dap_launch.continue_once     = true;
    app_config.dap_launch.working_directory = program_path.has_parent_path() ? program_path.parent_path().string() : std::filesystem::current_path().string();
}
