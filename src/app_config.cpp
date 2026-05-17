#include "app_config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>

namespace {

    std::string trim(std::string value) {
        const auto begin = std::ranges::find_if_not(value, [](unsigned char character) { return std::isspace(character) != 0; });
        const auto end   = std::ranges::find_if_not(std::ranges::reverse_view(value), [](unsigned char character) { return std::isspace(character) != 0; }).base();

        if (begin >= end) {
            return "";
        }

        return std::string(begin, end);
    }

    std::string stripComment(std::string value) {
        bool inside_quotes = false;
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (value[index] == '"') {
                inside_quotes = !inside_quotes;
                continue;
            }

            if (value[index] == '#' && !inside_quotes) {
                return value.substr(0, index);
            }
        }

        return value;
    }

    std::string unquote(std::string value) {
        value = trim(std::move(value));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            return value.substr(1, value.size() - 2);
        }

        return value;
    }

    bool parseBool(const std::string& value, bool fallback) {
        if (value == "true") {
            return true;
        }
        if (value == "false") {
            return false;
        }

        return fallback;
    }

    std::optional<ftxui::Color> parseHexColor(std::string value) {
        value = unquote(trim(std::move(value)));
        if (value.size() != 7 || value.front() != '#') {
            return std::nullopt;
        }

        const auto parse_channel = [&](std::size_t start) -> std::optional<std::uint8_t> {
            unsigned int channel = 0;
            const char*  begin   = value.data() + static_cast<std::ptrdiff_t>(start);
            const char*  end     = begin + 2;
            const auto   result  = std::from_chars(begin, end, channel, 16);
            if (result.ec != std::errc{} || result.ptr != end || channel > 255U) {
                return std::nullopt;
            }

            return static_cast<std::uint8_t>(channel);
        };

        const auto red   = parse_channel(1);
        const auto green = parse_channel(3);
        const auto blue  = parse_channel(5);
        if (!red.has_value() || !green.has_value() || !blue.has_value()) {
            return std::nullopt;
        }

        return ftxui::Color::RGB(red.value(), green.value(), blue.value());
    }

    std::vector<std::string> parseStringArray(std::string value) {
        value = trim(std::move(value));
        if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
            return {};
        }

        std::vector<std::string> items;
        std::string              current_item;
        bool                     inside_quotes = false;

        for (std::size_t index = 1; index + 1 < value.size(); ++index) {
            const char character = value[index];

            if (character == '"') {
                inside_quotes = !inside_quotes;
                current_item.push_back(character);
                continue;
            }

            if (character == ',' && !inside_quotes) {
                const auto parsed_item = unquote(trim(current_item));
                if (!parsed_item.empty()) {
                    items.push_back(parsed_item);
                }
                current_item.clear();
                continue;
            }

            current_item.push_back(character);
        }

        const auto parsed_item = unquote(trim(current_item));
        if (!parsed_item.empty()) {
            items.push_back(parsed_item);
        }

        return items;
    }

    void assignThemeOverride(SThemeOverrides& overrides, const std::string& key, const std::string& value) {
        const auto parsed_color = parseHexColor(value);
        if (!parsed_color.has_value()) {
            return;
        }

        if (key == "chrome") {
            overrides.chrome = parsed_color;
        } else if (key == "accent") {
            overrides.accent = parsed_color;
        } else if (key == "title") {
            overrides.title = parsed_color;
        } else if (key == "selected_foreground") {
            overrides.selected_foreground = parsed_color;
        } else if (key == "selected_background") {
            overrides.selected_background = parsed_color;
        } else if (key == "variable_name") {
            overrides.variable_name = parsed_color;
        } else if (key == "variable_type") {
            overrides.variable_type = parsed_color;
        } else if (key == "selected_variable_name") {
            overrides.selected_variable_name = parsed_color;
        } else if (key == "selected_variable_type") {
            overrides.selected_variable_type = parsed_color;
        } else if (key == "memory_address") {
            overrides.memory_address = parsed_color;
        } else if (key == "memory_hex") {
            overrides.memory_hex = parsed_color;
        } else if (key == "memory_ascii") {
            overrides.memory_ascii = parsed_color;
        } else if (key == "selected_memory_address") {
            overrides.selected_memory_address = parsed_color;
        } else if (key == "selected_memory_hex") {
            overrides.selected_memory_hex = parsed_color;
        } else if (key == "selected_memory_ascii") {
            overrides.selected_memory_ascii = parsed_color;
        } else if (key == "memory_highlight_hex") {
            overrides.memory_highlight_hex = parsed_color;
        } else if (key == "memory_highlight_hex_background") {
            overrides.memory_highlight_hex_background = parsed_color;
        } else if (key == "memory_highlight_ascii") {
            overrides.memory_highlight_ascii = parsed_color;
        } else if (key == "memory_highlight_ascii_background") {
            overrides.memory_highlight_ascii_background = parsed_color;
        } else if (key == "selected_memory_highlight_hex") {
            overrides.selected_memory_highlight_hex = parsed_color;
        } else if (key == "selected_memory_highlight_hex_background") {
            overrides.selected_memory_highlight_hex_background = parsed_color;
        } else if (key == "selected_memory_highlight_ascii") {
            overrides.selected_memory_highlight_ascii = parsed_color;
        } else if (key == "selected_memory_highlight_ascii_background") {
            overrides.selected_memory_highlight_ascii_background = parsed_color;
        } else if (key == "hint_key") {
            overrides.hint_key = parsed_color;
        } else if (key == "hint_description") {
            overrides.hint_description = parsed_color;
        } else if (key == "hint_specific_key") {
            overrides.hint_specific_key = parsed_color;
        } else if (key == "hint_specific_text") {
            overrides.hint_specific_text = parsed_color;
        } else if (key == "overlay_border") {
            overrides.overlay_border = parsed_color;
        }
    }

    eSessionMode parseSessionMode(const std::string& value, eSessionMode fallback) {
        if (value == "mock") {
            return eSessionMode::MOCK;
        }
        if (value == "dap_launch") {
            return eSessionMode::DAP_LAUNCH;
        }
        return fallback;
    }

    eFocusPane parseFocusPane(const std::string& value, eFocusPane fallback) {
        if (value == "locals") {
            return eFocusPane::LOCALS;
        }
        if (value == "memory") {
            return eFocusPane::MEMORY_VIEW;
        }
        if (value == "disassembly") {
            return eFocusPane::DISASSEMBLY_VIEW;
        }
        if (value == "watch_list") {
            return eFocusPane::WATCH_LIST;
        }

        return fallback;
    }

} // namespace

std::optional<SSourceBreakpointConfig> parseSourceBreakpointConfig(std::string value) {
    value                         = unquote(trim(std::move(value)));
    const auto separator_position = value.rfind(':');
    if (separator_position == std::string::npos || separator_position + 1 >= value.size()) {
        return std::nullopt;
    }

    const auto source_path = trim(value.substr(0, separator_position));
    const auto line_text   = trim(value.substr(separator_position + 1));
    if (source_path.empty() || line_text.empty()) {
        return std::nullopt;
    }

    std::int64_t line   = 0;
    const auto*  begin  = line_text.data();
    const auto*  end    = begin + line_text.size();
    const auto   result = std::from_chars(begin, end, line);
    if (result.ec != std::errc{} || result.ptr != end || line <= 0) {
        return std::nullopt;
    }

    return SSourceBreakpointConfig{
        .source_path = std::filesystem::path(source_path),
        .line        = line,
    };
}

SAppConfig loadAppConfig(const std::string& config_path) {
    SAppConfig    config = {};
    std::ifstream config_stream(config_path);
    if (!config_stream.is_open()) {
        return config;
    }

    std::string current_section;
    std::string line;
    while (std::getline(config_stream, line)) {
        line = trim(stripComment(std::move(line)));
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            current_section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        const auto separator_position = line.find('=');
        if (separator_position == std::string::npos) {
            continue;
        }

        const std::string key   = trim(line.substr(0, separator_position));
        std::string       value = trim(line.substr(separator_position + 1));
        if (!value.empty() && value.front() == '[' && value.back() != ']') {
            std::string continuation_line;
            while (std::getline(config_stream, continuation_line)) {
                continuation_line = trim(stripComment(std::move(continuation_line)));
                value += continuation_line;
                if (!continuation_line.empty() && continuation_line.back() == ']') {
                    break;
                }
            }
        }

        if (current_section == "views") {
            if (key == "show_memory") {
                config.show_memory_view = parseBool(value, config.show_memory_view);
            } else if (key == "show_disassembly") {
                config.show_disassembly_view = parseBool(value, config.show_disassembly_view);
            }
            continue;
        }

        if (current_section == "theme") {
            if (key == "preset") {
                config.theme_preset = parseThemePreset(unquote(value), config.theme_preset);
            } else {
                assignThemeOverride(config.theme_overrides, key, value);
            }
            continue;
        }

        if (current_section == "session") {
            if (key == "mode") {
                config.session_mode = parseSessionMode(unquote(value), config.session_mode);
            } else if (key == "startup_focus") {
                config.startup_focus = parseFocusPane(unquote(value), config.startup_focus);
            }
            continue;
        }

        if (current_section == "dap_launch") {
            if (key == "command") {
                config.dap_launch.command = unquote(value);
            } else if (key == "liblldb_path") {
                config.dap_launch.liblldb_path = unquote(value);
            } else if (key == "program") {
                config.dap_launch.program = unquote(value);
            } else if (key == "working_directory") {
                config.dap_launch.working_directory = unquote(value);
            } else if (key == "stop_on_entry") {
                config.dap_launch.stop_on_entry = parseBool(value, config.dap_launch.stop_on_entry);
            } else if (key == "continue_once") {
                config.dap_launch.continue_once = parseBool(value, config.dap_launch.continue_once);
            }
            continue;
        }

        if (current_section == "breakpoints") {
            if (key == "entries") {
                config.breakpoints.clear();
                for (const auto& item : parseStringArray(value)) {
                    const auto parsed_breakpoint = parseSourceBreakpointConfig(item);
                    if (parsed_breakpoint.has_value()) {
                        config.breakpoints.push_back(parsed_breakpoint.value());
                    }
                }
            }
            continue;
        }

        if (current_section == "codelldb.auto_detect") {
            if (key == "enabled") {
                config.codelldb_auto_detect.enabled = parseBool(value, config.codelldb_auto_detect.enabled);
            } else if (key == "candidate_roots") {
                config.codelldb_auto_detect.candidate_roots.clear();
                for (const auto& item : parseStringArray(value)) {
                    config.codelldb_auto_detect.candidate_roots.emplace_back(item);
                }
            }
            continue;
        }

        if (current_section == "keybindings") {
            const auto command = parseCommandName(key);
            if (!command.has_value()) {
                continue;
            }

            const std::string configured_keys = unquote(value);
            bool              updated         = false;
            for (auto& keybinding : config.keybindings) {
                if (keybinding.command != command.value()) {
                    continue;
                }

                keybinding.keys = configured_keys;
                updated         = true;
                break;
            }

            if (!updated) {
                config.keybindings.push_back({
                    .keys    = configured_keys,
                    .command = command.value(),
                });
            }
        }
    }

    return config;
}
