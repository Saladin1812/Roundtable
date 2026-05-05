#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <ftxui/screen/color.hpp>

enum class eThemePreset : std::uint8_t {
    DEFAULT,
    AMBER,
    ICE,
    FOREST,
};

struct SAppTheme {
    ftxui::Color chrome                                     = ftxui::Color::White;
    ftxui::Color accent                                     = ftxui::Color::CyanLight;
    ftxui::Color title                                      = ftxui::Color::White;
    ftxui::Color selected_foreground                        = ftxui::Color::Black;
    ftxui::Color selected_background                        = ftxui::Color::CyanLight;
    ftxui::Color variable_name                              = ftxui::Color::White;
    ftxui::Color variable_type                              = ftxui::Color::White;
    ftxui::Color selected_variable_name                     = ftxui::Color::White;
    ftxui::Color selected_variable_type                     = ftxui::Color::White;
    ftxui::Color memory_address                             = ftxui::Color::YellowLight;
    ftxui::Color memory_hex                                 = ftxui::Color::White;
    ftxui::Color memory_ascii                               = ftxui::Color::GreenLight;
    ftxui::Color selected_memory_address                    = ftxui::Color::YellowLight;
    ftxui::Color selected_memory_hex                        = ftxui::Color::White;
    ftxui::Color selected_memory_ascii                      = ftxui::Color::GreenLight;
    ftxui::Color memory_highlight_hex                       = ftxui::Color::Black;
    ftxui::Color memory_highlight_hex_background            = ftxui::Color::YellowLight;
    ftxui::Color memory_highlight_ascii                     = ftxui::Color::Black;
    ftxui::Color memory_highlight_ascii_background          = ftxui::Color::GreenLight;
    ftxui::Color selected_memory_highlight_hex              = ftxui::Color::Black;
    ftxui::Color selected_memory_highlight_hex_background   = ftxui::Color::YellowLight;
    ftxui::Color selected_memory_highlight_ascii            = ftxui::Color::Black;
    ftxui::Color selected_memory_highlight_ascii_background = ftxui::Color::GreenLight;
    ftxui::Color hint_key                                   = ftxui::Color::CyanLight;
    ftxui::Color hint_description                           = ftxui::Color::White;
    ftxui::Color hint_specific_key                          = ftxui::Color::YellowLight;
    ftxui::Color hint_specific_text                         = ftxui::Color::White;
    ftxui::Color overlay_border                             = ftxui::Color::CyanLight;
};

struct SThemeOverrides {
    std::optional<ftxui::Color> chrome;
    std::optional<ftxui::Color> accent;
    std::optional<ftxui::Color> title;
    std::optional<ftxui::Color> selected_foreground;
    std::optional<ftxui::Color> selected_background;
    std::optional<ftxui::Color> variable_name;
    std::optional<ftxui::Color> variable_type;
    std::optional<ftxui::Color> selected_variable_name;
    std::optional<ftxui::Color> selected_variable_type;
    std::optional<ftxui::Color> memory_address;
    std::optional<ftxui::Color> memory_hex;
    std::optional<ftxui::Color> memory_ascii;
    std::optional<ftxui::Color> selected_memory_address;
    std::optional<ftxui::Color> selected_memory_hex;
    std::optional<ftxui::Color> selected_memory_ascii;
    std::optional<ftxui::Color> memory_highlight_hex;
    std::optional<ftxui::Color> memory_highlight_hex_background;
    std::optional<ftxui::Color> memory_highlight_ascii;
    std::optional<ftxui::Color> memory_highlight_ascii_background;
    std::optional<ftxui::Color> selected_memory_highlight_hex;
    std::optional<ftxui::Color> selected_memory_highlight_hex_background;
    std::optional<ftxui::Color> selected_memory_highlight_ascii;
    std::optional<ftxui::Color> selected_memory_highlight_ascii_background;
    std::optional<ftxui::Color> hint_key;
    std::optional<ftxui::Color> hint_description;
    std::optional<ftxui::Color> hint_specific_key;
    std::optional<ftxui::Color> hint_specific_text;
    std::optional<ftxui::Color> overlay_border;
};

eThemePreset parseThemePreset(const std::string& value, eThemePreset fallback);
SAppTheme    buildTheme(eThemePreset preset);
SAppTheme    applyThemeOverrides(SAppTheme theme, const SThemeOverrides& overrides);
