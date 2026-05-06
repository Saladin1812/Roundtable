#include <catch2/catch_test_macros.hpp>

#include "cli_options.hpp"

TEST_CASE("parseCliOptions reads help flag") {
    char       arg0[] = "roundtable";
    char       arg1[] = "--help";
    char*      argv[] = {arg0, arg1};

    const auto options = parseCliOptions(2, argv);

    CHECK(options.show_help);
    CHECK_FALSE(options.launch_program.has_value());
}

TEST_CASE("parseCliOptions reads launch program path") {
    char       arg0[] = "roundtable";
    char       arg1[] = "/tmp/hello-world";
    char*      argv[] = {arg0, arg1};

    const auto options = parseCliOptions(2, argv);

    REQUIRE(options.launch_program.has_value());
    CHECK(options.launch_program.value() == std::filesystem::path("/tmp/hello-world"));
}

TEST_CASE("applyCliOverrides switches app config to dap_launch") {
    SAppConfig  app_config = {};
    SCliOptions options    = {
           .launch_program = std::filesystem::path("/tmp/hello-world"),
    };

    applyCliOverrides(options, app_config);

    CHECK(app_config.session_mode == eSessionMode::DAP_LAUNCH);
    CHECK(app_config.dap_launch.program == std::filesystem::path("/tmp/hello-world").string());
    CHECK(app_config.dap_launch.continue_once);
    CHECK(app_config.dap_launch.working_directory == std::filesystem::path("/tmp").string());
}
