#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "app_config.hpp"

TEST_CASE("loadAppConfig returns defaults when config file is missing") {
    const SAppConfig config = loadAppConfig("/tmp/roundtable-missing-config.toml");

    CHECK(config.session_mode == eSessionMode::MOCK);
    CHECK(config.startup_focus == eFocusPane::MEMORY_VIEW);
    CHECK(config.show_memory_view);
    CHECK_FALSE(config.show_disassembly_view);
    CHECK(config.theme_preset == eThemePreset::DEFAULT);
    CHECK(config.dap_launch.command.empty());
    REQUIRE_FALSE(config.keybindings.empty());
}

TEST_CASE("loadAppConfig reads views and keybinding overrides from TOML") {
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "roundtable-test-config.toml";

    {
        std::ofstream config_stream(config_path);
        config_stream << "[session]\n";
        config_stream << "mode = \"dap_launch\"\n";
        config_stream << "startup_focus = \"disassembly\"\n";
        config_stream << "\n";
        config_stream << "[views]\n";
        config_stream << "show_memory = false\n";
        config_stream << "show_disassembly = true\n";
        config_stream << "\n";
        config_stream << "[theme]\n";
        config_stream << "preset = \"forest\"\n";
        config_stream << "selected_background = \"#112233\"\n";
        config_stream << "selected_foreground = \"#eeeeee\"\n";
        config_stream << "variable_name = \"#abcdef\"\n";
        config_stream << "selected_variable_type = \"#123456\"\n";
        config_stream << "memory_address = \"#aa5500\"\n";
        config_stream << "selected_memory_hex = \"#445566\"\n";
        config_stream << "memory_highlight_ascii_background = \"#203040\"\n";
        config_stream << "selected_memory_highlight_hex = \"#fedcba\"\n";
        config_stream << "\n";
        config_stream << "[dap_launch]\n";
        config_stream << "command = \"/tmp/codelldb\"\n";
        config_stream << "liblldb_path = \"/tmp/liblldb.so\"\n";
        config_stream << "program = \"/tmp/sample\"\n";
        config_stream << "arguments = [\"--flag\", \"value\"]\n";
        config_stream << "working_directory = \"/tmp\"\n";
        config_stream << "stop_on_entry = false\n";
        config_stream << "continue_once = true\n";
        config_stream << "\n";
        config_stream << "[profiles.tests]\n";
        config_stream << "program = \"/tmp/sample-tests\"\n";
        config_stream << "arguments = [\"--suite\", \"unit\"]\n";
        config_stream << "working_directory = \"/tmp/tests\"\n";
        config_stream << "continue_once = false\n";
        config_stream << "\n";
        config_stream << "[breakpoints]\n";
        config_stream << "entries = [\n";
        config_stream << "  \"src/main.cpp:42\",\n";
        config_stream << "  \"/tmp/sample.cpp:7\",\n";
        config_stream << "]\n";
        config_stream << "\n";
        config_stream << "[watches]\n";
        config_stream << "entries = [\"sample_value\", \"ptr->field\"]\n";
        config_stream << "\n";
        config_stream << "[keybindings]\n";
        config_stream << "focus_threads = \"Space U\"\n";
        config_stream << "focus_memory = \"Space x\"\n";
        config_stream << "focus_breakpoints = \"Space P\"\n";
        config_stream << "remove_breakpoint = \"Space D\"\n";
        config_stream << "toggle_breakpoint = \"Space E\"\n";
        config_stream << "choose_profile = \"Space P\"\n";
        config_stream << "toggle_shortcuts_help = \"Space h\"\n";
        config_stream << "cycle_theme = \"Space C\"\n";
    }

    const SAppConfig config = loadAppConfig(config_path.string());

    CHECK(config.session_mode == eSessionMode::DAP_LAUNCH);
    CHECK(config.startup_focus == eFocusPane::DISASSEMBLY_VIEW);
    CHECK_FALSE(config.show_memory_view);
    CHECK(config.show_disassembly_view);
    CHECK(config.theme_preset == eThemePreset::FOREST);
    REQUIRE(config.theme_overrides.selected_background.has_value());
    REQUIRE(config.theme_overrides.selected_foreground.has_value());
    REQUIRE(config.theme_overrides.variable_name.has_value());
    REQUIRE(config.theme_overrides.selected_variable_type.has_value());
    REQUIRE(config.theme_overrides.memory_address.has_value());
    REQUIRE(config.theme_overrides.selected_memory_hex.has_value());
    REQUIRE(config.theme_overrides.memory_highlight_ascii_background.has_value());
    REQUIRE(config.theme_overrides.selected_memory_highlight_hex.has_value());
    CHECK(config.theme_overrides.selected_background.value() == ftxui::Color::RGB(0x11, 0x22, 0x33));
    CHECK(config.theme_overrides.selected_foreground.value() == ftxui::Color::RGB(0xee, 0xee, 0xee));
    CHECK(config.theme_overrides.variable_name.value() == ftxui::Color::RGB(0xab, 0xcd, 0xef));
    CHECK(config.theme_overrides.selected_variable_type.value() == ftxui::Color::RGB(0x12, 0x34, 0x56));
    CHECK(config.theme_overrides.memory_address.value() == ftxui::Color::RGB(0xaa, 0x55, 0x00));
    CHECK(config.theme_overrides.selected_memory_hex.value() == ftxui::Color::RGB(0x44, 0x55, 0x66));
    CHECK(config.theme_overrides.memory_highlight_ascii_background.value() == ftxui::Color::RGB(0x20, 0x30, 0x40));
    CHECK(config.theme_overrides.selected_memory_highlight_hex.value() == ftxui::Color::RGB(0xfe, 0xdc, 0xba));
    CHECK(config.dap_launch.command == "/tmp/codelldb");
    CHECK(config.dap_launch.liblldb_path == "/tmp/liblldb.so");
    CHECK(config.dap_launch.program == "/tmp/sample");
    REQUIRE(config.dap_launch.arguments.size() == 2);
    CHECK(config.dap_launch.arguments[0] == "--flag");
    CHECK(config.dap_launch.arguments[1] == "value");
    CHECK(config.dap_launch.working_directory == "/tmp");
    CHECK_FALSE(config.dap_launch.stop_on_entry);
    CHECK(config.dap_launch.continue_once);
    REQUIRE(config.launch_profiles.size() == 1);
    CHECK(config.launch_profiles[0].name == "tests");
    REQUIRE(config.launch_profiles[0].program.has_value());
    CHECK(config.launch_profiles[0].program.value() == "/tmp/sample-tests");
    REQUIRE(config.launch_profiles[0].arguments.has_value());
    CHECK(config.launch_profiles[0].arguments.value()[0] == "--suite");
    CHECK(config.launch_profiles[0].arguments.value()[1] == "unit");
    REQUIRE(config.breakpoints.size() == 2);
    CHECK(config.breakpoints[0].source_path == std::filesystem::path("src/main.cpp"));
    CHECK(config.breakpoints[0].line == 42);
    CHECK(config.breakpoints[0].enabled);
    CHECK(config.breakpoints[1].source_path == std::filesystem::path("/tmp/sample.cpp"));
    CHECK(config.breakpoints[1].line == 7);
    CHECK(config.breakpoints[1].enabled);
    REQUIRE(config.watches.size() == 2);
    CHECK(config.watches[0] == "sample_value");
    CHECK(config.watches[1] == "ptr->field");

    const auto threads_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::FOCUS_THREADS; });
    REQUIRE(threads_keybinding != config.keybindings.end());
    CHECK(threads_keybinding->keys == "Space U");

    const auto memory_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::FOCUS_MEMORY; });
    REQUIRE(memory_keybinding != config.keybindings.end());
    CHECK(memory_keybinding->keys == "Space x");

    const auto breakpoints_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::FOCUS_BREAKPOINTS; });
    REQUIRE(breakpoints_keybinding != config.keybindings.end());
    CHECK(breakpoints_keybinding->keys == "Space P");

    const auto remove_breakpoint_keybinding =
        std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::REMOVE_BREAKPOINT; });
    REQUIRE(remove_breakpoint_keybinding != config.keybindings.end());
    CHECK(remove_breakpoint_keybinding->keys == "Space D");

    const auto toggle_breakpoint_keybinding =
        std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::TOGGLE_BREAKPOINT; });
    REQUIRE(toggle_breakpoint_keybinding != config.keybindings.end());
    CHECK(toggle_breakpoint_keybinding->keys == "Space E");

    const auto choose_profile_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::CHOOSE_PROFILE; });
    REQUIRE(choose_profile_keybinding != config.keybindings.end());
    CHECK(choose_profile_keybinding->keys == "Space P");

    const auto help_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::TOGGLE_SHORTCUTS_HELP; });
    REQUIRE(help_keybinding != config.keybindings.end());
    CHECK(help_keybinding->keys == "Space h");

    const auto theme_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::CYCLE_THEME; });
    REQUIRE(theme_keybinding != config.keybindings.end());
    CHECK(theme_keybinding->keys == "Space C");

    std::filesystem::remove(config_path);
}

TEST_CASE("loadAppConfig applies selected launch profile as an overlay") {
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "roundtable-test-profile-config.toml";

    {
        std::ofstream config_stream(config_path);
        config_stream << "[session]\n";
        config_stream << "profile = \"tests\"\n";
        config_stream << "\n";
        config_stream << "[dap_launch]\n";
        config_stream << "command = \"/tmp/codelldb\"\n";
        config_stream << "liblldb_path = \"/tmp/liblldb.so\"\n";
        config_stream << "program = \"/tmp/app\"\n";
        config_stream << "arguments = []\n";
        config_stream << "working_directory = \"/tmp/app-dir\"\n";
        config_stream << "stop_on_entry = true\n";
        config_stream << "continue_once = true\n";
        config_stream << "\n";
        config_stream << "[profiles.tests]\n";
        config_stream << "program = \"/tmp/tests\"\n";
        config_stream << "arguments = [\"--unit\"]\n";
        config_stream << "working_directory = \"/tmp/test-dir\"\n";
        config_stream << "continue_once = false\n";
    }

    const SAppConfigLoadResult result = loadAppConfigWithDiagnostics(config_path.string());

    CHECK(result.diagnostics.empty());
    CHECK(result.config.session_mode == eSessionMode::DAP_LAUNCH);
    CHECK(result.config.active_profile == "tests");
    CHECK(result.config.dap_launch.command == "/tmp/codelldb");
    CHECK(result.config.dap_launch.liblldb_path == "/tmp/liblldb.so");
    CHECK(result.config.dap_launch.program == "/tmp/tests");
    REQUIRE(result.config.dap_launch.arguments.size() == 1);
    CHECK(result.config.dap_launch.arguments[0] == "--unit");
    CHECK(result.config.dap_launch.working_directory == "/tmp/test-dir");
    CHECK(result.config.dap_launch.stop_on_entry);
    CHECK_FALSE(result.config.dap_launch.continue_once);

    std::filesystem::remove(config_path);
}

TEST_CASE("loadAppConfig reads codelldb auto-detect configuration from TOML") {
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "roundtable-test-codelldb-autodetect-config.toml";

    {
        std::ofstream config_stream(config_path);
        config_stream << "[codelldb.auto_detect]\n";
        config_stream << "enabled = false\n";
        config_stream << "candidate_roots = [\"/opt/codelldb/one\", \"/opt/codelldb/two\"]\n";
    }

    const SAppConfig config = loadAppConfig(config_path.string());

    CHECK_FALSE(config.codelldb_auto_detect.enabled);
    REQUIRE(config.codelldb_auto_detect.candidate_roots.size() == 2);
    CHECK(config.codelldb_auto_detect.candidate_roots[0] == std::filesystem::path("/opt/codelldb/one"));
    CHECK(config.codelldb_auto_detect.candidate_roots[1] == std::filesystem::path("/opt/codelldb/two"));

    std::filesystem::remove(config_path);
}

TEST_CASE("loadAppConfigWithDiagnostics reports malformed config entries") {
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "roundtable-test-invalid-config.toml";

    {
        std::ofstream config_stream(config_path);
        config_stream << "[session]\n";
        config_stream << "mode = \"invalid_mode\"\n";
        config_stream << "startup_focus = \"bad_pane\"\n";
        config_stream << "\n";
        config_stream << "[views]\n";
        config_stream << "show_memory = maybe\n";
        config_stream << "\n";
        config_stream << "[theme]\n";
        config_stream << "selected_background = \"not-a-color\"\n";
        config_stream << "\n";
        config_stream << "[breakpoints]\n";
        config_stream << "entries = [\"missing-line\"]\n";
        config_stream << "\n";
        config_stream << "[keybindings]\n";
        config_stream << "not_a_command = \"Space Z\"\n";
        config_stream << "\n";
        config_stream << "[profiles.bad]\n";
        config_stream << "continue_once = maybe\n";
        config_stream << "unknown = true\n";
        config_stream << "\n";
        config_stream << "[unknown]\n";
        config_stream << "value = true\n";
        config_stream << "not valid\n";
    }

    const SAppConfigLoadResult result = loadAppConfigWithDiagnostics(config_path.string());

    const auto                 has_diagnostic = [&](const std::string& text) {
        return std::ranges::any_of(result.diagnostics, [&](const std::string& diagnostic) { return diagnostic.find(text) != std::string::npos; });
    };

    CHECK(has_diagnostic("unknown session mode"));
    CHECK(has_diagnostic("unknown startup_focus"));
    CHECK(has_diagnostic("invalid boolean for views.show_memory"));
    CHECK(has_diagnostic("invalid or unknown theme color"));
    CHECK(has_diagnostic("invalid breakpoint entry"));
    CHECK(has_diagnostic("unknown keybinding command"));
    CHECK(has_diagnostic("invalid boolean for profiles.bad.continue_once"));
    CHECK(has_diagnostic("unknown launch profile key"));
    CHECK(has_diagnostic("unknown section [unknown]"));
    CHECK(has_diagnostic("expected key = value"));

    std::filesystem::remove(config_path);
}
