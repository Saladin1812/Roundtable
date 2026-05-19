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

TEST_CASE("parseCliOptions reads explicit config path") {
    char       arg0[] = "roundtable";
    char       arg1[] = "--config";
    char       arg2[] = "/tmp/roundtable-generated.toml";
    char*      argv[] = {arg0, arg1, arg2};

    const auto options = parseCliOptions(3, argv);

    CHECK(options.config_path == std::filesystem::path("/tmp/roundtable-generated.toml"));
    CHECK_FALSE(options.launch_program.has_value());
}

TEST_CASE("parseCliOptions reads config path and launch program") {
    char       arg0[] = "roundtable";
    char       arg1[] = "--config=/tmp/roundtable-generated.toml";
    char       arg2[] = "/tmp/hello-world";
    char*      argv[] = {arg0, arg1, arg2};

    const auto options = parseCliOptions(3, argv);

    CHECK(options.config_path == std::filesystem::path("/tmp/roundtable-generated.toml"));
    REQUIRE(options.launch_program.has_value());
    CHECK(options.launch_program.value() == std::filesystem::path("/tmp/hello-world"));
}

TEST_CASE("parseCliOptions reads profile") {
    char       arg0[] = "roundtable";
    char       arg1[] = "--profile=tests";
    char*      argv[] = {arg0, arg1};

    const auto options = parseCliOptions(2, argv);

    REQUIRE(options.profile.has_value());
    CHECK(options.profile.value() == "tests");
}

TEST_CASE("applyCliOverrides switches app config to dap_launch") {
    SAppConfig  app_config = {};
    SCliOptions options    = {
           .show_help      = false,
           .config_path    = "roundtable.toml",
           .profile        = std::nullopt,
           .launch_program = std::filesystem::path("/tmp/hello-world"),
    };

    applyCliOverrides(options, app_config);

    CHECK(app_config.session_mode == eSessionMode::DAP_LAUNCH);
    CHECK(app_config.dap_launch.program == std::filesystem::path("/tmp/hello-world").string());
    CHECK(app_config.dap_launch.continue_once);
    CHECK(app_config.dap_launch.working_directory == std::filesystem::path("/tmp").string());
}

TEST_CASE("applyCliOverrides applies selected launch profile") {
    SAppConfig app_config = {
        .dap_launch =
            {
                .command           = "/tmp/codelldb",
                .liblldb_path      = "/tmp/liblldb.so",
                .program           = "/tmp/app",
                .arguments         = {},
                .working_directory = "/tmp/app-dir",
                .stop_on_entry     = true,
                .continue_once     = true,
            },
        .launch_profiles =
            {
                {
                    .name              = "tests",
                    .command           = std::nullopt,
                    .liblldb_path      = std::nullopt,
                    .program           = "/tmp/tests",
                    .arguments         = std::vector<std::string>{"--unit"},
                    .working_directory = "/tmp/test-dir",
                    .stop_on_entry     = std::nullopt,
                    .continue_once     = false,
                    .watches           = std::nullopt,
                    .breakpoints       = std::nullopt,
                },
            },
    };
    SCliOptions options = {
        .show_help      = false,
        .config_path    = "roundtable.toml",
        .profile        = "tests",
        .launch_program = std::nullopt,
    };

    applyCliOverrides(options, app_config);

    CHECK(app_config.session_mode == eSessionMode::DAP_LAUNCH);
    CHECK(app_config.active_profile == "tests");
    CHECK(app_config.dap_launch.command == "/tmp/codelldb");
    CHECK(app_config.dap_launch.program == "/tmp/tests");
    REQUIRE(app_config.dap_launch.arguments.size() == 1);
    CHECK(app_config.dap_launch.arguments[0] == "--unit");
    CHECK(app_config.dap_launch.working_directory == "/tmp/test-dir");
    CHECK_FALSE(app_config.dap_launch.continue_once);
}
