#include "app_theme.hpp"

eThemePreset parseThemePreset(const std::string& value, eThemePreset fallback) {
    if (value == "default") {
        return eThemePreset::DEFAULT;
    }
    if (value == "amber") {
        return eThemePreset::AMBER;
    }
    if (value == "ice") {
        return eThemePreset::ICE;
    }
    if (value == "forest") {
        return eThemePreset::FOREST;
    }

    return fallback;
}

SAppTheme buildTheme(eThemePreset preset) {
    switch (preset) {
        case eThemePreset::AMBER:
            return {
                .chrome              = ftxui::Color::White,
                .accent              = ftxui::Color::YellowLight,
                .title               = ftxui::Color::YellowLight,
                .selected_foreground = ftxui::Color::White,
                .selected_background = ftxui::Color::RGB(96, 62, 0),
                .memory_address      = ftxui::Color::YellowLight,
                .memory_hex          = ftxui::Color::White,
                .memory_ascii        = ftxui::Color::GreenLight,
                .hint_key            = ftxui::Color::YellowLight,
                .hint_description    = ftxui::Color::White,
                .hint_specific_key   = ftxui::Color::RedLight,
                .hint_specific_text  = ftxui::Color::White,
                .overlay_border      = ftxui::Color::YellowLight,
            };
        case eThemePreset::ICE:
            return {
                .chrome              = ftxui::Color::White,
                .accent              = ftxui::Color::CyanLight,
                .title               = ftxui::Color::CyanLight,
                .selected_foreground = ftxui::Color::White,
                .selected_background = ftxui::Color::RGB(16, 68, 92),
                .memory_address      = ftxui::Color::BlueLight,
                .memory_hex          = ftxui::Color::White,
                .memory_ascii        = ftxui::Color::CyanLight,
                .hint_key            = ftxui::Color::CyanLight,
                .hint_description    = ftxui::Color::White,
                .hint_specific_key   = ftxui::Color::BlueLight,
                .hint_specific_text  = ftxui::Color::White,
                .overlay_border      = ftxui::Color::CyanLight,
            };
        case eThemePreset::FOREST:
            return {
                .chrome              = ftxui::Color::White,
                .accent              = ftxui::Color::GreenLight,
                .title               = ftxui::Color::GreenLight,
                .selected_foreground = ftxui::Color::White,
                .selected_background = ftxui::Color::RGB(18, 78, 44),
                .memory_address      = ftxui::Color::YellowLight,
                .memory_hex          = ftxui::Color::White,
                .memory_ascii        = ftxui::Color::GreenLight,
                .hint_key            = ftxui::Color::GreenLight,
                .hint_description    = ftxui::Color::White,
                .hint_specific_key   = ftxui::Color::YellowLight,
                .hint_specific_text  = ftxui::Color::White,
                .overlay_border      = ftxui::Color::GreenLight,
            };
        case eThemePreset::DEFAULT:
        default:
            return {
                .chrome              = ftxui::Color::White,
                .accent              = ftxui::Color::CyanLight,
                .title               = ftxui::Color::White,
                .selected_foreground = ftxui::Color::White,
                .selected_background = ftxui::Color::RGB(52, 52, 52),
                .memory_address      = ftxui::Color::YellowLight,
                .memory_hex          = ftxui::Color::White,
                .memory_ascii        = ftxui::Color::GreenLight,
                .hint_key            = ftxui::Color::CyanLight,
                .hint_description    = ftxui::Color::White,
                .hint_specific_key   = ftxui::Color::YellowLight,
                .hint_specific_text  = ftxui::Color::White,
                .overlay_border      = ftxui::Color::CyanLight,
            };
    }
}
