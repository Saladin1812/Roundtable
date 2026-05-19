#pragma once

#include <iosfwd>
#include <string>

#include "app_config.hpp"
#include "cli_options.hpp"

struct SAppStartupResult {
    bool        should_exit = false;
    int         exit_code   = 0;
    SCliOptions cli_options = {};
    std::string config_path;
    SAppConfig  app_config = {};
};

void              printUsage(std::ostream& output);
SAppStartupResult initializeAppStartup(int argc, char** argv, std::ostream& output);
