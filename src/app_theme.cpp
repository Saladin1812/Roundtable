#include "app_theme.hpp"

namespace {

    void applyOverride(ftxui::Color& target, const std::optional<ftxui::Color>& override_color) {
        if (override_color.has_value()) {
            target = override_color.value();
        }
    }

} // namespace

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
                .chrome                                     = ftxui::Color::White,
                .accent                                     = ftxui::Color::YellowLight,
                .title                                      = ftxui::Color::YellowLight,
                .selected_foreground                        = ftxui::Color::White,
                .selected_background                        = ftxui::Color::RGB(96, 62, 0),
                .variable_name                              = ftxui::Color::White,
                .variable_type                              = ftxui::Color::YellowLight,
                .selected_variable_name                     = ftxui::Color::White,
                .selected_variable_type                     = ftxui::Color::YellowLight,
                .memory_address                             = ftxui::Color::YellowLight,
                .memory_hex                                 = ftxui::Color::White,
                .memory_ascii                               = ftxui::Color::GreenLight,
                .selected_memory_address                    = ftxui::Color::YellowLight,
                .selected_memory_hex                        = ftxui::Color::White,
                .selected_memory_ascii                      = ftxui::Color::GreenLight,
                .memory_highlight_hex                       = ftxui::Color::Black,
                .memory_highlight_hex_background            = ftxui::Color::YellowLight,
                .memory_highlight_ascii                     = ftxui::Color::Black,
                .memory_highlight_ascii_background          = ftxui::Color::GreenLight,
                .selected_memory_highlight_hex              = ftxui::Color::Black,
                .selected_memory_highlight_hex_background   = ftxui::Color::YellowLight,
                .selected_memory_highlight_ascii            = ftxui::Color::Black,
                .selected_memory_highlight_ascii_background = ftxui::Color::GreenLight,
                .hint_key                                   = ftxui::Color::YellowLight,
                .hint_description                           = ftxui::Color::White,
                .hint_specific_key                          = ftxui::Color::RedLight,
                .hint_specific_text                         = ftxui::Color::White,
                .overlay_border                             = ftxui::Color::YellowLight,
            };
        case eThemePreset::ICE:
            return {
                .chrome                                     = ftxui::Color::White,
                .accent                                     = ftxui::Color::CyanLight,
                .title                                      = ftxui::Color::CyanLight,
                .selected_foreground                        = ftxui::Color::White,
                .selected_background                        = ftxui::Color::RGB(16, 68, 92),
                .variable_name                              = ftxui::Color::White,
                .variable_type                              = ftxui::Color::BlueLight,
                .selected_variable_name                     = ftxui::Color::White,
                .selected_variable_type                     = ftxui::Color::CyanLight,
                .memory_address                             = ftxui::Color::BlueLight,
                .memory_hex                                 = ftxui::Color::White,
                .memory_ascii                               = ftxui::Color::CyanLight,
                .selected_memory_address                    = ftxui::Color::BlueLight,
                .selected_memory_hex                        = ftxui::Color::White,
                .selected_memory_ascii                      = ftxui::Color::CyanLight,
                .memory_highlight_hex                       = ftxui::Color::Black,
                .memory_highlight_hex_background            = ftxui::Color::CyanLight,
                .memory_highlight_ascii                     = ftxui::Color::Black,
                .memory_highlight_ascii_background          = ftxui::Color::BlueLight,
                .selected_memory_highlight_hex              = ftxui::Color::Black,
                .selected_memory_highlight_hex_background   = ftxui::Color::CyanLight,
                .selected_memory_highlight_ascii            = ftxui::Color::Black,
                .selected_memory_highlight_ascii_background = ftxui::Color::BlueLight,
                .hint_key                                   = ftxui::Color::CyanLight,
                .hint_description                           = ftxui::Color::White,
                .hint_specific_key                          = ftxui::Color::BlueLight,
                .hint_specific_text                         = ftxui::Color::White,
                .overlay_border                             = ftxui::Color::CyanLight,
            };
        case eThemePreset::FOREST:
            return {
                .chrome                                     = ftxui::Color::White,
                .accent                                     = ftxui::Color::GreenLight,
                .title                                      = ftxui::Color::GreenLight,
                .selected_foreground                        = ftxui::Color::White,
                .selected_background                        = ftxui::Color::RGB(18, 78, 44),
                .variable_name                              = ftxui::Color::White,
                .variable_type                              = ftxui::Color::YellowLight,
                .selected_variable_name                     = ftxui::Color::White,
                .selected_variable_type                     = ftxui::Color::YellowLight,
                .memory_address                             = ftxui::Color::YellowLight,
                .memory_hex                                 = ftxui::Color::White,
                .memory_ascii                               = ftxui::Color::GreenLight,
                .selected_memory_address                    = ftxui::Color::YellowLight,
                .selected_memory_hex                        = ftxui::Color::White,
                .selected_memory_ascii                      = ftxui::Color::GreenLight,
                .memory_highlight_hex                       = ftxui::Color::Black,
                .memory_highlight_hex_background            = ftxui::Color::YellowLight,
                .memory_highlight_ascii                     = ftxui::Color::Black,
                .memory_highlight_ascii_background          = ftxui::Color::GreenLight,
                .selected_memory_highlight_hex              = ftxui::Color::Black,
                .selected_memory_highlight_hex_background   = ftxui::Color::YellowLight,
                .selected_memory_highlight_ascii            = ftxui::Color::Black,
                .selected_memory_highlight_ascii_background = ftxui::Color::GreenLight,
                .hint_key                                   = ftxui::Color::GreenLight,
                .hint_description                           = ftxui::Color::White,
                .hint_specific_key                          = ftxui::Color::YellowLight,
                .hint_specific_text                         = ftxui::Color::White,
                .overlay_border                             = ftxui::Color::GreenLight,
            };
        case eThemePreset::DEFAULT:
        default:
            return {
                .chrome                                     = ftxui::Color::White,
                .accent                                     = ftxui::Color::CyanLight,
                .title                                      = ftxui::Color::White,
                .selected_foreground                        = ftxui::Color::White,
                .selected_background                        = ftxui::Color::RGB(52, 52, 52),
                .variable_name                              = ftxui::Color::White,
                .variable_type                              = ftxui::Color::CyanLight,
                .selected_variable_name                     = ftxui::Color::White,
                .selected_variable_type                     = ftxui::Color::CyanLight,
                .memory_address                             = ftxui::Color::YellowLight,
                .memory_hex                                 = ftxui::Color::White,
                .memory_ascii                               = ftxui::Color::GreenLight,
                .selected_memory_address                    = ftxui::Color::YellowLight,
                .selected_memory_hex                        = ftxui::Color::White,
                .selected_memory_ascii                      = ftxui::Color::GreenLight,
                .memory_highlight_hex                       = ftxui::Color::Black,
                .memory_highlight_hex_background            = ftxui::Color::YellowLight,
                .memory_highlight_ascii                     = ftxui::Color::Black,
                .memory_highlight_ascii_background          = ftxui::Color::GreenLight,
                .selected_memory_highlight_hex              = ftxui::Color::Black,
                .selected_memory_highlight_hex_background   = ftxui::Color::YellowLight,
                .selected_memory_highlight_ascii            = ftxui::Color::Black,
                .selected_memory_highlight_ascii_background = ftxui::Color::GreenLight,
                .hint_key                                   = ftxui::Color::CyanLight,
                .hint_description                           = ftxui::Color::White,
                .hint_specific_key                          = ftxui::Color::YellowLight,
                .hint_specific_text                         = ftxui::Color::White,
                .overlay_border                             = ftxui::Color::CyanLight,
            };
    }
}

SAppTheme applyThemeOverrides(SAppTheme theme, const SThemeOverrides& overrides) {
    applyOverride(theme.chrome, overrides.chrome);
    applyOverride(theme.accent, overrides.accent);
    applyOverride(theme.title, overrides.title);
    applyOverride(theme.selected_foreground, overrides.selected_foreground);
    applyOverride(theme.selected_background, overrides.selected_background);
    applyOverride(theme.variable_name, overrides.variable_name);
    applyOverride(theme.variable_type, overrides.variable_type);
    applyOverride(theme.selected_variable_name, overrides.selected_variable_name);
    applyOverride(theme.selected_variable_type, overrides.selected_variable_type);
    applyOverride(theme.memory_address, overrides.memory_address);
    applyOverride(theme.memory_hex, overrides.memory_hex);
    applyOverride(theme.memory_ascii, overrides.memory_ascii);
    applyOverride(theme.selected_memory_address, overrides.selected_memory_address);
    applyOverride(theme.selected_memory_hex, overrides.selected_memory_hex);
    applyOverride(theme.selected_memory_ascii, overrides.selected_memory_ascii);
    applyOverride(theme.memory_highlight_hex, overrides.memory_highlight_hex);
    applyOverride(theme.memory_highlight_hex_background, overrides.memory_highlight_hex_background);
    applyOverride(theme.memory_highlight_ascii, overrides.memory_highlight_ascii);
    applyOverride(theme.memory_highlight_ascii_background, overrides.memory_highlight_ascii_background);
    applyOverride(theme.selected_memory_highlight_hex, overrides.selected_memory_highlight_hex);
    applyOverride(theme.selected_memory_highlight_hex_background, overrides.selected_memory_highlight_hex_background);
    applyOverride(theme.selected_memory_highlight_ascii, overrides.selected_memory_highlight_ascii);
    applyOverride(theme.selected_memory_highlight_ascii_background, overrides.selected_memory_highlight_ascii_background);
    applyOverride(theme.hint_key, overrides.hint_key);
    applyOverride(theme.hint_description, overrides.hint_description);
    applyOverride(theme.hint_specific_key, overrides.hint_specific_key);
    applyOverride(theme.hint_specific_text, overrides.hint_specific_text);
    applyOverride(theme.overlay_border, overrides.overlay_border);
    return theme;
}
