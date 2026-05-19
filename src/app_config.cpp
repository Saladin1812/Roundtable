#include "app_config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

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

    std::optional<bool> parseBoolValue(const std::string& value) {
        if (value == "true") {
            return true;
        }
        if (value == "false") {
            return false;
        }

        return std::nullopt;
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

    bool assignThemeOverride(SThemeOverrides& overrides, const std::string& key, const std::string& value) {
        const auto parsed_color = parseHexColor(value);
        if (!parsed_color.has_value()) {
            return false;
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
        } else {
            return false;
        }

        return true;
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
        if (value == "threads") {
            return eFocusPane::THREADS;
        }
        if (value == "stack") {
            return eFocusPane::STACK;
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
        if (value == "breakpoints") {
            return eFocusPane::BREAKPOINTS;
        }

        return fallback;
    }

    bool isStringArraySyntax(std::string value) {
        value = trim(std::move(value));
        return value.size() >= 2 && value.front() == '[' && value.back() == ']';
    }

    bool isLaunchProfileSection(const std::string& section) {
        constexpr std::string_view prefix = "profiles.";
        return section.starts_with(prefix) && section.size() > prefix.size();
    }

    std::string launchProfileNameFromSection(const std::string& section) {
        constexpr std::string_view prefix = "profiles.";
        return section.substr(prefix.size());
    }

    SLaunchProfileConfig& findOrAddLaunchProfile(SAppConfig& config, const std::string& name) {
        const auto profile_iterator = std::ranges::find_if(config.launch_profiles, [&](const SLaunchProfileConfig& profile) { return profile.name == name; });
        if (profile_iterator != config.launch_profiles.end()) {
            return *profile_iterator;
        }

        config.launch_profiles.push_back({
            .name              = name,
            .command           = std::nullopt,
            .liblldb_path      = std::nullopt,
            .program           = std::nullopt,
            .arguments         = std::nullopt,
            .working_directory = std::nullopt,
            .stop_on_entry     = std::nullopt,
            .continue_once     = std::nullopt,
            .watches           = std::nullopt,
            .breakpoints       = std::nullopt,
        });
        return config.launch_profiles.back();
    }

    void applyLaunchProfileConfig(SAppConfig& config, const SLaunchProfileConfig& profile) {
        config.session_mode = eSessionMode::DAP_LAUNCH;
        if (profile.command.has_value()) {
            config.dap_launch.command = profile.command.value();
        }
        if (profile.liblldb_path.has_value()) {
            config.dap_launch.liblldb_path = profile.liblldb_path.value();
        }
        if (profile.program.has_value()) {
            config.dap_launch.program = profile.program.value();
        }
        if (profile.arguments.has_value()) {
            config.dap_launch.arguments = profile.arguments.value();
        }
        if (profile.working_directory.has_value()) {
            config.dap_launch.working_directory = profile.working_directory.value();
        }
        if (profile.stop_on_entry.has_value()) {
            config.dap_launch.stop_on_entry = profile.stop_on_entry.value();
        }
        if (profile.continue_once.has_value()) {
            config.dap_launch.continue_once = profile.continue_once.value();
        }
        if (profile.watches.has_value()) {
            config.watches = profile.watches.value();
        }
        if (profile.breakpoints.has_value()) {
            config.breakpoints = profile.breakpoints.value();
        }
    }

    std::string lineDiagnostic(std::size_t line_number, const std::string& message) {
        return "line " + std::to_string(line_number) + ": " + message;
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
        .source_path          = std::filesystem::path(source_path),
        .line                 = line,
        .enabled              = true,
        .adapter_status_known = false,
        .adapter_verified     = false,
        .adapter_line         = 0,
        .adapter_message      = "",
    };
}

SAppConfigLoadResult loadAppConfigWithDiagnostics(const std::string& config_path) {
    SAppConfigLoadResult result = {};
    SAppConfig&          config = result.config;
    std::ifstream        config_stream(config_path);
    if (!config_stream.is_open()) {
        result.diagnostics.push_back("config file not found: " + config_path);
        return result;
    }

    std::string current_section;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(config_stream, line)) {
        ++line_number;
        line = trim(stripComment(std::move(line)));
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                result.diagnostics.push_back(lineDiagnostic(line_number, "malformed section header"));
                continue;
            }

            current_section = trim(line.substr(1, line.size() - 2));
            if (current_section != "views" && current_section != "theme" && current_section != "session" && current_section != "dap_launch" && current_section != "breakpoints" &&
                current_section != "watches" && current_section != "codelldb.auto_detect" && current_section != "keybindings" && !isLaunchProfileSection(current_section)) {
                result.diagnostics.push_back(lineDiagnostic(line_number, "unknown section [" + current_section + "]"));
            }
            continue;
        }

        const auto separator_position = line.find('=');
        if (separator_position == std::string::npos) {
            result.diagnostics.push_back(lineDiagnostic(line_number, "expected key = value"));
            continue;
        }

        const std::string key               = trim(line.substr(0, separator_position));
        std::string       value             = trim(line.substr(separator_position + 1));
        const std::size_t value_line_number = line_number;
        if (current_section.empty()) {
            result.diagnostics.push_back(lineDiagnostic(line_number, "key outside a section: " + key));
            continue;
        }
        if (!value.empty() && value.front() == '[' && value.back() != ']') {
            std::string continuation_line;
            bool        closed_array = false;
            while (std::getline(config_stream, continuation_line)) {
                ++line_number;
                continuation_line = trim(stripComment(std::move(continuation_line)));
                value += continuation_line;
                if (!continuation_line.empty() && continuation_line.back() == ']') {
                    closed_array = true;
                    break;
                }
            }
            if (!closed_array) {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unterminated array for " + key));
            }
        }

        if (current_section == "views") {
            if (key == "show_memory") {
                const auto parsed_value = parseBoolValue(value);
                if (parsed_value.has_value()) {
                    config.show_memory_view = parsed_value.value();
                } else {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid boolean for views.show_memory"));
                }
            } else if (key == "show_disassembly") {
                const auto parsed_value = parseBoolValue(value);
                if (parsed_value.has_value()) {
                    config.show_disassembly_view = parsed_value.value();
                } else {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid boolean for views.show_disassembly"));
                }
            } else {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown views key: " + key));
            }
            continue;
        }

        if (current_section == "theme") {
            if (key == "preset") {
                const auto unquoted_value = unquote(value);
                const auto parsed_value   = parseThemePreset(unquoted_value, config.theme_preset);
                if (parsed_value == config.theme_preset && unquoted_value != "default" && unquoted_value != "amber" && unquoted_value != "ice" && unquoted_value != "forest") {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown theme preset: " + unquoted_value));
                }
                config.theme_preset = parsed_value;
            } else {
                if (!assignThemeOverride(config.theme_overrides, key, value)) {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid or unknown theme color: " + key));
                }
            }
            continue;
        }

        if (current_section == "session") {
            if (key == "mode") {
                const auto unquoted_value = unquote(value);
                const auto parsed_value   = parseSessionMode(unquoted_value, config.session_mode);
                if (parsed_value == config.session_mode && unquoted_value != "mock" && unquoted_value != "dap_launch") {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown session mode: " + unquoted_value));
                }
                config.session_mode = parsed_value;
            } else if (key == "startup_focus") {
                const auto unquoted_value = unquote(value);
                const auto parsed_value   = parseFocusPane(unquoted_value, config.startup_focus);
                if (parsed_value == config.startup_focus && unquoted_value != "locals" && unquoted_value != "threads" && unquoted_value != "stack" && unquoted_value != "memory" &&
                    unquoted_value != "disassembly" && unquoted_value != "watch_list" && unquoted_value != "breakpoints") {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown startup_focus: " + unquoted_value));
                }
                config.startup_focus = parsed_value;
            } else if (key == "profile") {
                config.active_profile = unquote(value);
            } else {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown session key: " + key));
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
            } else if (key == "arguments") {
                if (!isStringArraySyntax(value)) {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid string array for dap_launch.arguments"));
                }
                config.dap_launch.arguments = parseStringArray(value);
            } else if (key == "working_directory") {
                config.dap_launch.working_directory = unquote(value);
            } else if (key == "stop_on_entry") {
                const auto parsed_value = parseBoolValue(value);
                if (parsed_value.has_value()) {
                    config.dap_launch.stop_on_entry = parsed_value.value();
                } else {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid boolean for dap_launch.stop_on_entry"));
                }
            } else if (key == "continue_once") {
                const auto parsed_value = parseBoolValue(value);
                if (parsed_value.has_value()) {
                    config.dap_launch.continue_once = parsed_value.value();
                } else {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid boolean for dap_launch.continue_once"));
                }
            } else {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown dap_launch key: " + key));
            }
            continue;
        }

        if (isLaunchProfileSection(current_section)) {
            auto& profile = findOrAddLaunchProfile(config, launchProfileNameFromSection(current_section));
            if (key == "command") {
                profile.command = unquote(value);
            } else if (key == "liblldb_path") {
                profile.liblldb_path = unquote(value);
            } else if (key == "program") {
                profile.program = unquote(value);
            } else if (key == "arguments") {
                if (!isStringArraySyntax(value)) {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid string array for " + current_section + ".arguments"));
                }
                profile.arguments = parseStringArray(value);
            } else if (key == "working_directory") {
                profile.working_directory = unquote(value);
            } else if (key == "stop_on_entry") {
                const auto parsed_value = parseBoolValue(value);
                if (parsed_value.has_value()) {
                    profile.stop_on_entry = parsed_value;
                } else {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid boolean for " + current_section + ".stop_on_entry"));
                }
            } else if (key == "continue_once") {
                const auto parsed_value = parseBoolValue(value);
                if (parsed_value.has_value()) {
                    profile.continue_once = parsed_value;
                } else {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid boolean for " + current_section + ".continue_once"));
                }
            } else if (key == "watches") {
                if (!isStringArraySyntax(value)) {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid string array for " + current_section + ".watches"));
                }
                profile.watches = parseStringArray(value);
            } else if (key == "breakpoints") {
                if (!isStringArraySyntax(value)) {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid string array for " + current_section + ".breakpoints"));
                }

                std::vector<SSourceBreakpointConfig> parsed_breakpoints;
                for (const auto& item : parseStringArray(value)) {
                    const auto parsed_breakpoint = parseSourceBreakpointConfig(item);
                    if (parsed_breakpoint.has_value()) {
                        parsed_breakpoints.push_back(parsed_breakpoint.value());
                    } else {
                        result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid launch profile breakpoint entry: " + item));
                    }
                }
                profile.breakpoints = std::move(parsed_breakpoints);
            } else {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown launch profile key: " + key));
            }
            continue;
        }

        if (current_section == "breakpoints") {
            if (key == "entries") {
                if (!isStringArraySyntax(value)) {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid string array for breakpoints.entries"));
                }
                config.breakpoints.clear();
                for (const auto& item : parseStringArray(value)) {
                    const auto parsed_breakpoint = parseSourceBreakpointConfig(item);
                    if (parsed_breakpoint.has_value()) {
                        config.breakpoints.push_back(parsed_breakpoint.value());
                    } else {
                        result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid breakpoint entry: " + item));
                    }
                }
            } else {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown breakpoints key: " + key));
            }
            continue;
        }

        if (current_section == "watches") {
            if (key == "entries") {
                if (!isStringArraySyntax(value)) {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid string array for watches.entries"));
                }
                config.watches = parseStringArray(value);
            } else {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown watches key: " + key));
            }
            continue;
        }

        if (current_section == "codelldb.auto_detect") {
            if (key == "enabled") {
                const auto parsed_value = parseBoolValue(value);
                if (parsed_value.has_value()) {
                    config.codelldb_auto_detect.enabled = parsed_value.value();
                } else {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid boolean for codelldb.auto_detect.enabled"));
                }
            } else if (key == "candidate_roots") {
                if (!isStringArraySyntax(value)) {
                    result.diagnostics.push_back(lineDiagnostic(value_line_number, "invalid string array for codelldb.auto_detect.candidate_roots"));
                }
                config.codelldb_auto_detect.candidate_roots.clear();
                for (const auto& item : parseStringArray(value)) {
                    config.codelldb_auto_detect.candidate_roots.emplace_back(item);
                }
            } else {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown codelldb.auto_detect key: " + key));
            }
            continue;
        }

        if (current_section == "keybindings") {
            const auto command = parseCommandName(key);
            if (!command.has_value()) {
                result.diagnostics.push_back(lineDiagnostic(value_line_number, "unknown keybinding command: " + key));
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
            continue;
        }

        std::string diagnostic_message = "ignored key in unknown section [";
        diagnostic_message += current_section;
        diagnostic_message += "]: ";
        diagnostic_message += key;
        result.diagnostics.push_back(lineDiagnostic(value_line_number, diagnostic_message));
    }

    if (!config.active_profile.empty()) {
        const auto profile_iterator = std::ranges::find_if(config.launch_profiles, [&](const SLaunchProfileConfig& profile) { return profile.name == config.active_profile; });
        if (profile_iterator != config.launch_profiles.end()) {
            applyLaunchProfileConfig(config, *profile_iterator);
        } else {
            result.diagnostics.push_back("unknown launch profile: " + config.active_profile);
        }
    }

    return result;
}

SAppConfig loadAppConfig(const std::string& config_path) {
    return loadAppConfigWithDiagnostics(config_path).config;
}

bool applyLaunchProfile(SAppConfig& config, const std::string& profile_name) {
    const auto profile_iterator = std::ranges::find_if(config.launch_profiles, [&](const SLaunchProfileConfig& profile) { return profile.name == profile_name; });
    if (profile_iterator == config.launch_profiles.end()) {
        return false;
    }

    config.active_profile = profile_name;
    applyLaunchProfileConfig(config, *profile_iterator);
    return true;
}
