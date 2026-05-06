#include "cli_options.hpp"

SCliOptions parseCliOptions(int argc, char** argv) {
    SCliOptions options = {};

    for (int argument_index = 1; argument_index < argc; ++argument_index) {
        const std::string argument = argv[argument_index];
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            continue;
        }

        if (!options.launch_program.has_value()) {
            options.launch_program = std::filesystem::path(argument);
        }
    }

    return options;
}

void applyCliOverrides(const SCliOptions& cli_options, SAppConfig& app_config) {
    if (!cli_options.launch_program.has_value()) {
        return;
    }

    const auto program_path = std::filesystem::absolute(cli_options.launch_program.value());

    app_config.session_mode                 = eSessionMode::DAP_LAUNCH;
    app_config.dap_launch.program           = program_path.string();
    app_config.dap_launch.continue_once     = true;
    app_config.dap_launch.working_directory = program_path.has_parent_path() ? program_path.parent_path().string() : std::filesystem::current_path().string();
}
