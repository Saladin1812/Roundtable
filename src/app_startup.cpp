#include "app_startup.hpp"

#include <ostream>

#ifndef ROUNDTABLE_VERSION
#define ROUNDTABLE_VERSION "0.1.0"
#endif

void printUsage(std::ostream& output) {
    output << "Usage: roundtable [--config path] [program-path]\n";
    output << "  roundtable                         Start with roundtable.toml, user config, or defaults\n";
    output << "  roundtable --config /tmp/rt.toml   Start with an explicit config file\n";
    output << "  roundtable --profile tests         Use [profiles.tests] from config\n";
    output << "  roundtable --init-config           Create a user config file if missing\n";
    output << "  roundtable --init-config --force   Overwrite the user config file\n";
    output << "  roundtable --mock                  Start the built-in mock session for UI testing\n";
    output << "  roundtable --version               Print version and exit\n";
    output << "  roundtable ./mybinary              Force dap_launch for the given binary\n";
}

SAppStartupResult initializeAppStartup(int argc, char** argv, std::ostream& output) {
    SAppStartupResult result = {
        .should_exit = false,
        .exit_code   = 0,
        .cli_options = parseCliOptions(argc, argv),
        .config_path = {},
        .app_config  = {},
    };

    if (result.cli_options.show_help) {
        printUsage(output);
        result.should_exit = true;
        return result;
    }

    if (result.cli_options.show_version) {
        output << "roundtable " << ROUNDTABLE_VERSION << '\n';
        result.should_exit = true;
        return result;
    }

    if (result.cli_options.init_config) {
        const auto init_result = initializeUserAppConfig(result.cli_options.force_init_config);
        output << init_result.message << '\n';
        result.should_exit = true;
        result.exit_code   = init_result.ok ? 0 : 1;
        return result;
    }

    result.config_path            = result.cli_options.config_path.string();
    const auto config_load_result = loadAppConfigForCliWithDiagnostics(result.config_path, result.cli_options.config_path_explicit);
    result.app_config             = config_load_result.config;
    applyCliOverrides(result.cli_options, result.app_config);
    return result;
}
