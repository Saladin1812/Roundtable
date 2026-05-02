#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "app_config.hpp"

TEST_CASE("loadAppConfig returns defaults when config file is missing") {
    const SAppConfig config = loadAppConfig("/tmp/roundtable-missing-config.toml");

    CHECK(config.show_memory_view);
    CHECK_FALSE(config.show_disassembly_view);
    REQUIRE_FALSE(config.keybindings.empty());
}

TEST_CASE("loadAppConfig reads views and keybinding overrides from TOML") {
    const std::filesystem::path config_path = std::filesystem::temp_directory_path() / "roundtable-test-config.toml";

    {
        std::ofstream config_stream(config_path);
        config_stream << "[views]\n";
        config_stream << "show_memory = false\n";
        config_stream << "show_disassembly = true\n";
        config_stream << "\n";
        config_stream << "[keybindings]\n";
        config_stream << "focus_memory = \"Space x\"\n";
        config_stream << "toggle_shortcuts_help = \"Space h\"\n";
    }

    const SAppConfig config = loadAppConfig(config_path.string());

    CHECK_FALSE(config.show_memory_view);
    CHECK(config.show_disassembly_view);

    const auto memory_keybinding = std::find_if(config.keybindings.begin(), config.keybindings.end(), [](const SKeybinding& keybinding) {
        return keybinding.command == eCommand::FOCUS_MEMORY;
    });
    REQUIRE(memory_keybinding != config.keybindings.end());
    CHECK(memory_keybinding->keys == "Space x");

    const auto help_keybinding = std::find_if(config.keybindings.begin(), config.keybindings.end(), [](const SKeybinding& keybinding) {
        return keybinding.command == eCommand::TOGGLE_SHORTCUTS_HELP;
    });
    REQUIRE(help_keybinding != config.keybindings.end());
    CHECK(help_keybinding->keys == "Space h");

    std::filesystem::remove(config_path);
}
