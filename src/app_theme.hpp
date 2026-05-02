#pragma once

#include <cstdint>
#include <string>

#include <ftxui/screen/color.hpp>

enum class eThemePreset : std::uint8_t {
    DEFAULT,
    AMBER,
    ICE,
    FOREST,
};

struct SAppTheme {
    ftxui::Color chrome              = ftxui::Color::White;
    ftxui::Color accent              = ftxui::Color::CyanLight;
    ftxui::Color title               = ftxui::Color::White;
    ftxui::Color selected_foreground = ftxui::Color::Black;
    ftxui::Color selected_background = ftxui::Color::CyanLight;
    ftxui::Color memory_address      = ftxui::Color::YellowLight;
    ftxui::Color memory_hex          = ftxui::Color::White;
    ftxui::Color memory_ascii        = ftxui::Color::GreenLight;
    ftxui::Color hint_key            = ftxui::Color::CyanLight;
    ftxui::Color hint_description    = ftxui::Color::White;
    ftxui::Color hint_specific_key   = ftxui::Color::YellowLight;
    ftxui::Color hint_specific_text  = ftxui::Color::White;
    ftxui::Color overlay_border      = ftxui::Color::CyanLight;
};

eThemePreset parseThemePreset(const std::string& value, eThemePreset fallback);
SAppTheme    buildTheme(eThemePreset preset);
