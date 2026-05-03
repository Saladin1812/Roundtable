#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
        config_stream << "\n";
        config_stream << "[dap_launch]\n";
        config_stream << "command = \"/tmp/codelldb\"\n";
        config_stream << "liblldb_path = \"/tmp/liblldb.so\"\n";
        config_stream << "program = \"/tmp/sample\"\n";
        config_stream << "working_directory = \"/tmp\"\n";
        config_stream << "stop_on_entry = false\n";
        config_stream << "continue_once = true\n";
        config_stream << "\n";
        config_stream << "[keybindings]\n";
        config_stream << "focus_memory = \"Space x\"\n";
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
    CHECK(config.theme_overrides.selected_background.value() == ftxui::Color::RGB(0x11, 0x22, 0x33));
    CHECK(config.theme_overrides.selected_foreground.value() == ftxui::Color::RGB(0xee, 0xee, 0xee));
    CHECK(config.theme_overrides.variable_name.value() == ftxui::Color::RGB(0xab, 0xcd, 0xef));
    CHECK(config.theme_overrides.selected_variable_type.value() == ftxui::Color::RGB(0x12, 0x34, 0x56));
    CHECK(config.theme_overrides.memory_address.value() == ftxui::Color::RGB(0xaa, 0x55, 0x00));
    CHECK(config.theme_overrides.selected_memory_hex.value() == ftxui::Color::RGB(0x44, 0x55, 0x66));
    CHECK(config.dap_launch.command == "/tmp/codelldb");
    CHECK(config.dap_launch.liblldb_path == "/tmp/liblldb.so");
    CHECK(config.dap_launch.program == "/tmp/sample");
    CHECK(config.dap_launch.working_directory == "/tmp");
    CHECK_FALSE(config.dap_launch.stop_on_entry);
    CHECK(config.dap_launch.continue_once);

    const auto memory_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::FOCUS_MEMORY; });
    REQUIRE(memory_keybinding != config.keybindings.end());
    CHECK(memory_keybinding->keys == "Space x");

    const auto help_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::TOGGLE_SHORTCUTS_HELP; });
    REQUIRE(help_keybinding != config.keybindings.end());
    CHECK(help_keybinding->keys == "Space h");

    const auto theme_keybinding = std::ranges::find_if(config.keybindings, [](const SKeybinding& keybinding) { return keybinding.command == eCommand::CYCLE_THEME; });
    REQUIRE(theme_keybinding != config.keybindings.end());
    CHECK(theme_keybinding->keys == "Space C");

    std::filesystem::remove(config_path);
}
