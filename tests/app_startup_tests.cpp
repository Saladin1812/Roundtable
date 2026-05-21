#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "app_startup.hpp"

TEST_CASE("initializeAppStartup prints usage and exits for help") {
    char               arg0[] = "roundtable";
    char               arg1[] = "--help";
    char*              argv[] = {arg0, arg1};

    std::ostringstream output;
    const auto         result = initializeAppStartup(2, argv, output);

    CHECK(result.should_exit);
    CHECK(result.exit_code == 0);
    CHECK(output.str().find("Usage: roundtable") != std::string::npos);
    CHECK(output.str().find("--init-config") != std::string::npos);
    CHECK(output.str().find("--mock") != std::string::npos);
    CHECK(output.str().find("--version") != std::string::npos);
}

TEST_CASE("initializeAppStartup applies mock override") {
    char               arg0[] = "roundtable";
    char               arg1[] = "--mock";
    char*              argv[] = {arg0, arg1};

    std::ostringstream output;
    const auto         result = initializeAppStartup(2, argv, output);

    CHECK_FALSE(result.should_exit);
    CHECK(result.app_config.session_mode == eSessionMode::MOCK);
}

TEST_CASE("initializeAppStartup prints version and exits") {
    char               arg0[] = "roundtable";
    char               arg1[] = "--version";
    char*              argv[] = {arg0, arg1};

    std::ostringstream output;
    const auto         result = initializeAppStartup(2, argv, output);

    CHECK(result.should_exit);
    CHECK(result.exit_code == 0);
    CHECK(output.str().find("roundtable ") != std::string::npos);
}

TEST_CASE("initializeAppStartup loads config and applies program override") {
    const std::filesystem::path config_path  = std::filesystem::temp_directory_path() / "roundtable-startup-test.toml";
    const std::filesystem::path program_path = std::filesystem::temp_directory_path() / "roundtable-startup-program";

    {
        std::ofstream config_stream(config_path);
        config_stream << "[theme]\n";
        config_stream << "preset = \"forest\"\n";
        config_stream << "\n";
        config_stream << "[session]\n";
        config_stream << "mode = \"mock\"\n";
    }

    char               arg0[]       = "roundtable";
    char               arg1[]       = "--config";
    auto               config_text  = config_path.string();
    char*              arg2         = config_text.data();
    auto               program_text = program_path.string();
    char*              arg3         = program_text.data();
    char*              argv[]       = {arg0, arg1, arg2, arg3};

    std::ostringstream output;
    const auto         result = initializeAppStartup(4, argv, output);

    CHECK_FALSE(result.should_exit);
    CHECK(result.config_path == config_path.string());
    CHECK(result.app_config.theme_preset == eThemePreset::FOREST);
    CHECK(result.app_config.session_mode == eSessionMode::DAP_LAUNCH);
    CHECK(result.app_config.dap_launch.program == std::filesystem::absolute(program_path).string());
    CHECK_FALSE(result.app_config.dap_launch.continue_once);

    std::filesystem::remove(config_path);
}
