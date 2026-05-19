#include <array>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "app_config.hpp"
#include "app_startup.hpp"
#include "app_theme.hpp"
#include "dap_session.hpp"
#include "debugger_controller.hpp"
#include "debug_session.hpp"
#include "pane_refresh.hpp"
#include "pane_state.hpp"
#include "session_status.hpp"

namespace {

    constexpr std::array<eThemePreset, 4> kThemePresets = {
        eThemePreset::DEFAULT,
        eThemePreset::AMBER,
        eThemePreset::ICE,
        eThemePreset::FOREST,
    };

    enum class ePromptMode : std::uint8_t {
        NONE,
        ADD_WATCH,
        EDIT_WATCH,
        ADD_BREAKPOINT,
        MEMORY_TARGET,
    };

    enum class eWatchActionMode : std::uint8_t {
        NONE,
        EDIT,
        REMOVE,
        MOVE_UP,
        MOVE_DOWN,
        DUPLICATE,
    };

    enum class eBreakpointActionMode : std::uint8_t {
        NONE,
        REMOVE,
    };

    struct SPromptState {
        ePromptMode mode = ePromptMode::NONE;
        std::string input;
        std::size_t cursor_index     = 0;
        bool        replace_on_input = false;
    };

    struct SThemePickerState {
        bool         active          = false;
        eThemePreset original_preset = eThemePreset::DEFAULT;
        std::size_t  selected_index  = 0;
    };

    struct SProfilePickerState {
        bool        active         = false;
        std::size_t selected_index = 0;
    };

    struct SAsyncDapControlState {
        std::atomic_bool running             = false;
        std::atomic_bool pause_requested     = false;
        std::atomic_bool terminate_requested = false;
        std::atomic_bool quit_requested      = false;
        std::jthread     worker;
    };

    SPromptState beginPrompt(ePromptMode mode, std::string initial_input = "", bool replace_on_input = false) {
        return {
            .mode             = mode,
            .input            = std::move(initial_input),
            .cursor_index     = initial_input.size(),
            .replace_on_input = replace_on_input,
        };
    }

    std::string buildPromptDisplay(const SPromptState& prompt_state) {
        std::string display_input = prompt_state.input;
        const auto  cursor_index  = std::min(prompt_state.cursor_index, display_input.size());
        display_input.insert(cursor_index, "|");
        return "> " + display_input;
    }

    std::string themePresetName(eThemePreset preset) {
        switch (preset) {
            case eThemePreset::DEFAULT: return "default";
            case eThemePreset::AMBER: return "amber";
            case eThemePreset::ICE: return "ice";
            case eThemePreset::FOREST: return "forest";
        }

        return "default";
    }

    std::size_t themePresetIndex(eThemePreset preset) {
        for (std::size_t index = 0; index < kThemePresets.size(); ++index) {
            if (kThemePresets[index] == preset) {
                return index;
            }
        }

        return 0;
    }

    std::size_t launchProfileIndex(const std::vector<SLaunchProfileConfig>& profiles, const std::string& active_profile) {
        for (std::size_t index = 0; index < profiles.size(); ++index) {
            if (profiles[index].name == active_profile) {
                return index;
            }
        }

        return 0;
    }

    std::string compactPathForStatus(std::string source_path) {
        if (source_path.empty()) {
            return "<unknown>";
        }

        const auto last_separator = source_path.find_last_of("/\\");
        if (last_separator == std::string::npos) {
            return source_path;
        }

        return source_path.substr(last_separator + 1);
    }

    std::string compactPathWithParent(std::string source_path) {
        if (source_path.empty()) {
            return "<unknown>";
        }

        const auto filename_start = source_path.find_last_of("/\\");
        if (filename_start == std::string::npos) {
            return source_path;
        }

        const auto parent_end   = filename_start;
        const auto parent_start = source_path.find_last_of("/\\", parent_end == 0 ? 0 : parent_end - 1);
        if (parent_start == std::string::npos) {
            return source_path;
        }

        return ".../" + source_path.substr(parent_start + 1);
    }

    SStoppedLocation stackFrameLocation(const SStoppedStackFrame& stack_frame) {
        return {
            .function_name = stack_frame.function_name,
            .source_path   = stack_frame.source_path,
            .line          = stack_frame.line,
            .column        = stack_frame.column,
        };
    }

    std::vector<std::string> formatStackPaneRows(const std::vector<SStoppedStackFrame>& stack_frames) {
        std::vector<std::string> rows;
        rows.reserve(stack_frames.size());

        for (std::size_t index = 0; index < stack_frames.size(); ++index) {
            const auto& stack_frame = stack_frames[index];
            std::string row         = "#" + std::to_string(index) + " ";
            row += stack_frame.function_name.empty() ? "<unknown>" : stack_frame.function_name;
            if (!stack_frame.source_path.empty()) {
                row += " ";
                row += compactPathForStatus(stack_frame.source_path);
                if (stack_frame.line > 0) {
                    row += ":" + std::to_string(stack_frame.line);
                }
            }
            rows.push_back(std::move(row));
        }

        return rows;
    }

    std::vector<std::string> formatThreadPaneRows(const std::vector<SStoppedThread>& threads) {
        std::vector<std::string> rows;
        rows.reserve(threads.size());

        for (std::size_t index = 0; index < threads.size(); ++index) {
            const auto& thread = threads[index];
            std::string row    = "#" + std::to_string(index) + " T:" + std::to_string(thread.id);
            if (!thread.name.empty()) {
                row += " " + thread.name;
            }
            rows.push_back(std::move(row));
        }

        return rows;
    }

    std::size_t selectedThreadIndex(const std::vector<SStoppedThread>& threads, std::int64_t thread_id) {
        for (std::size_t index = 0; index < threads.size(); ++index) {
            if (threads[index].id == thread_id) {
                return index;
            }
        }

        return 0;
    }

    std::vector<std::string> formatBreakpointPaneRows(const std::vector<SSourceBreakpointConfig>& breakpoints) {
        std::vector<std::string> rows;
        rows.reserve(breakpoints.size());

        for (std::size_t index = 0; index < breakpoints.size(); ++index) {
            const auto&       breakpoint = breakpoints[index];
            const std::string state      = breakpoint.enabled ? "[x] " : "[ ] ";
            std::string       status;
            if (!breakpoint.enabled) {
                status = "off";
            } else if (!breakpoint.adapter_status_known) {
                status = "pending";
            } else if (breakpoint.adapter_verified) {
                status = "ok";
            } else {
                status = "fail";
            }

            std::string row = state + status + " #" + std::to_string(index) + " :" + std::to_string(breakpoint.line) + " " + compactPathWithParent(breakpoint.source_path.string());
            if (breakpoint.enabled && breakpoint.adapter_status_known && breakpoint.adapter_line > 0 && breakpoint.adapter_line != breakpoint.line) {
                row += " -> :" + std::to_string(breakpoint.adapter_line);
            }
            if (breakpoint.enabled && !breakpoint.adapter_message.empty()) {
                row += " - " + breakpoint.adapter_message;
            }

            rows.push_back(std::move(row));
        }

        return rows;
    }

    struct SLeaderHintRow {
        std::string keys;
        std::string description;
        eCommand    command       = eCommand::FOCUS_MEMORY;
        bool        pane_specific = false;
    };

    int commandPriority(eCommand command, eFocusPane focused_pane) {
        switch (focused_pane) {
            case eFocusPane::THREADS:
                if (command == eCommand::FOCUS_STACK || command == eCommand::FOCUS_LOCALS) {
                    return 0;
                }
                break;
            case eFocusPane::STACK:
                if (command == eCommand::FOCUS_THREADS || command == eCommand::FOCUS_LOCALS || command == eCommand::FOCUS_MEMORY) {
                    return 0;
                }
                break;
            case eFocusPane::WATCH_LIST:
                if (command == eCommand::EDIT_WATCH || command == eCommand::REMOVE_WATCH || command == eCommand::ADD_WATCH || command == eCommand::MOVE_WATCH_UP ||
                    command == eCommand::MOVE_WATCH_DOWN || command == eCommand::DUPLICATE_WATCH) {
                    return 0;
                }
                if (command == eCommand::SET_MEMORY_TARGET) {
                    return 1;
                }
                break;
            case eFocusPane::BREAKPOINTS:
                if (command == eCommand::ADD_BREAKPOINT || command == eCommand::REMOVE_BREAKPOINT || command == eCommand::TOGGLE_BREAKPOINT) {
                    return 0;
                }
                break;
            case eFocusPane::LOCALS:
                if (command == eCommand::SET_MEMORY_TARGET || command == eCommand::FOCUS_MEMORY || command == eCommand::FOCUS_THREADS || command == eCommand::FOCUS_STACK) {
                    return 1;
                }
                break;
            case eFocusPane::MEMORY_VIEW:
                if (command == eCommand::SET_MEMORY_TARGET || command == eCommand::TOGGLE_DISASSEMBLY) {
                    return 0;
                }
                break;
            case eFocusPane::DISASSEMBLY_VIEW:
                if (command == eCommand::TOGGLE_MEMORY || command == eCommand::FOCUS_MEMORY) {
                    return 0;
                }
                break;
        }

        return 2;
    }

    bool isPaneSpecificCommand(eCommand command, eFocusPane focused_pane) {
        return commandPriority(command, focused_pane) == 0;
    }

    std::vector<SLeaderHintRow> buildLeaderHintRows(const std::vector<SKeybinding>& keybindings, eFocusPane focused_pane) {
        std::vector<SLeaderHintRow> hints;
        hints.reserve(keybindings.size());

        for (const auto& keybinding : keybindings) {
            if (!keybinding.keys.starts_with("Space ") || keybinding.keys.size() <= 6) {
                continue;
            }

            hints.push_back({
                .keys          = keybinding.keys.substr(6),
                .description   = commandDescription(keybinding.command),
                .command       = keybinding.command,
                .pane_specific = isPaneSpecificCommand(keybinding.command, focused_pane),
            });
        }

        std::ranges::stable_sort(hints, [&](const SLeaderHintRow& left, const SLeaderHintRow& right) {
            return commandPriority(left.command, focused_pane) < commandPriority(right.command, focused_pane);
        });

        return hints;
    }

    std::optional<std::tuple<std::string, std::string, std::string>> splitMemoryRow(const std::string& row) {
        const auto first_separator = row.find("  ");
        if (first_separator == std::string::npos) {
            return std::nullopt;
        }

        const auto second_separator = row.find("  ", first_separator + 2);
        if (second_separator == std::string::npos) {
            return std::nullopt;
        }

        return std::make_tuple(row.substr(0, first_separator), row.substr(first_separator + 2, second_separator - (first_separator + 2)), row.substr(second_separator + 2));
    }

    std::optional<std::tuple<std::string, std::string, std::string>> splitLocalRow(const std::string& row) {
        const auto type_separator = row.find(" : ");
        if (type_separator == std::string::npos) {
            return std::nullopt;
        }

        const auto value_separator = row.find(" = ", type_separator + 3);
        if (value_separator == std::string::npos) {
            return std::nullopt;
        }

        return std::make_tuple(row.substr(0, type_separator), row.substr(type_separator + 3, value_separator - (type_separator + 3)), row.substr(value_separator + 3));
    }

    std::optional<std::tuple<std::string, std::string, std::string, bool>> splitWatchRow(const std::string& row) {
        const auto value_separator = row.find(" = ");
        if (value_separator != std::string::npos) {
            const auto type_separator = row.rfind(" : ");
            if (type_separator != std::string::npos && type_separator > value_separator) {
                return std::make_tuple(row.substr(0, value_separator), row.substr(value_separator + 3, type_separator - (value_separator + 3)), row.substr(type_separator + 3),
                                       true);
            }
        }

        const auto error_separator = row.find(" : ");
        if (error_separator == std::string::npos) {
            return std::nullopt;
        }

        return std::make_tuple(row.substr(0, error_separator), row.substr(error_separator + 3), std::string{}, false);
    }

    std::optional<std::tuple<std::string, std::string>> splitWatchErrorRow(const std::string& row) {
        const auto error_separator = row.find(" ! ");
        if (error_separator == std::string::npos) {
            return std::nullopt;
        }

        return std::make_tuple(row.substr(0, error_separator), row.substr(error_separator + 3));
    }

    std::vector<std::string> splitMemoryByteTokens(const std::string& hex_bytes) {
        std::vector<std::string> tokens;
        std::size_t              token_start = 0;

        while (token_start < hex_bytes.size()) {
            const auto separator = hex_bytes.find(' ', token_start);
            if (separator == std::string::npos) {
                tokens.push_back(hex_bytes.substr(token_start));
                break;
            }

            if (separator > token_start) {
                tokens.push_back(hex_bytes.substr(token_start, separator - token_start));
            }

            token_start = separator + 1;
        }

        return tokens;
    }

    bool isHighlightedMemoryByte(const SMemoryByteHighlight& highlight, std::uint64_t row_address, std::size_t row_index, std::size_t byte_index) {
        if (highlight.byte_count == 0) {
            return false;
        }

        if (highlight.synthetic) {
            const std::size_t absolute_offset = (row_index * highlight.row_stride) + byte_index;
            return absolute_offset >= highlight.start_offset && absolute_offset < highlight.start_offset + highlight.byte_count;
        }

        const std::uint64_t absolute_address = row_address + static_cast<std::uint64_t>(byte_index);
        return absolute_address >= highlight.start_address && absolute_address < highlight.start_address + highlight.byte_count;
    }

    ftxui::Element renderMemoryRow(const std::string& row, std::size_t row_index, bool is_selected, const SAppTheme& theme, const SMemoryRenderContext& memory_context) {
        using namespace ftxui;

        const auto memory_parts = splitMemoryRow(row);
        if (!memory_parts.has_value()) {
            auto fallback = text(row) | color(is_selected ? theme.selected_foreground : theme.memory_ascii);
            if (is_selected) {
                fallback = fallback | bgcolor(theme.selected_background);
            }
            return fallback;
        }

        const auto& [address, hex_bytes, ascii] = memory_parts.value();
        const auto    byte_tokens               = splitMemoryByteTokens(hex_bytes);
        std::uint64_t row_address               = 0;
        try {
            row_address = std::stoull(address, nullptr, 0);
        } catch (const std::exception&) { row_address = 0; }

        Elements hex_elements;
        Elements ascii_elements;
        hex_elements.reserve(byte_tokens.size() * 2);
        ascii_elements.reserve(ascii.size());

        const auto default_hex_color          = is_selected ? theme.selected_memory_hex : theme.memory_hex;
        const auto default_ascii_color        = is_selected ? theme.selected_memory_ascii : theme.memory_ascii;
        const auto row_background             = theme.selected_background;
        const auto highlight_hex_background   = theme.memory_highlight_hex_background;
        const auto highlight_ascii_background = theme.memory_highlight_ascii_background;

        for (std::size_t byte_index = 0; byte_index < byte_tokens.size(); ++byte_index) {
            const bool highlighted = memory_context.highlight.has_value() && isHighlightedMemoryByte(memory_context.highlight.value(), row_address, row_index, byte_index);

            auto       hex_element =
                text(byte_tokens[byte_index]) | color(highlighted ? (is_selected ? theme.selected_memory_highlight_hex : theme.memory_highlight_hex) : default_hex_color);
            auto ascii_element = text(byte_index < ascii.size() ? std::string(1, ascii[byte_index]) : "") |
                color(highlighted ? (is_selected ? theme.selected_memory_highlight_ascii : theme.memory_highlight_ascii) : default_ascii_color);

            if (is_selected && !highlighted) {
                hex_element = hex_element | bgcolor(row_background);
            }
            if (is_selected && !highlighted) {
                ascii_element = ascii_element | bgcolor(row_background);
            }

            if (highlighted) {
                hex_element   = hex_element | bgcolor(highlight_hex_background);
                ascii_element = ascii_element | bgcolor(highlight_ascii_background);
            }

            hex_elements.push_back(hex_element);
            if (byte_index + 1 < byte_tokens.size()) {
                auto       separator = text(" ") | color(default_hex_color);
                const bool next_highlighted =
                    memory_context.highlight.has_value() && isHighlightedMemoryByte(memory_context.highlight.value(), row_address, row_index, byte_index + 1);
                if (is_selected && !(highlighted && next_highlighted)) {
                    separator = separator | bgcolor(row_background);
                }
                if (highlighted && next_highlighted) {
                    separator = separator | bgcolor(highlight_hex_background);
                }
                hex_elements.push_back(separator);
            }
            ascii_elements.push_back(ascii_element);
        }

        auto address_element = text(address) | color(is_selected ? theme.selected_memory_address : theme.memory_address);
        auto spacing_element = text("  ") | color(default_hex_color);
        auto middle_spacing  = text("  ") | color(default_hex_color);

        if (is_selected) {
            address_element = address_element | bgcolor(row_background);
            spacing_element = spacing_element | bgcolor(row_background);
            middle_spacing  = middle_spacing | bgcolor(row_background);
        }

        return hbox({
            address_element,
            spacing_element,
            hbox(std::move(hex_elements)),
            middle_spacing,
            hbox(std::move(ascii_elements)),
        });
    }

    ftxui::Element renderPaneRow(const std::string& row, std::size_t row_index, bool is_selected, const SAppTheme& theme, const std::string& pane_title, bool is_memory_pane,
                                 const SMemoryRenderContext& memory_context) {
        using namespace ftxui;

        Element row_element;
        if (is_memory_pane) {
            row_element = renderMemoryRow(row, row_index, is_selected, theme, memory_context);
        } else if (pane_title.find("Locals") != std::string::npos) {
            const auto local_parts = splitLocalRow(row);
            if (local_parts.has_value()) {
                const auto& [name, type, value] = local_parts.value();
                row_element                     = hbox({
                    text(name) | color(is_selected ? theme.selected_variable_name : theme.variable_name),
                    text(" : ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                    text(type) | color(is_selected ? theme.selected_variable_type : theme.variable_type),
                    text(" = ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                    text(value) | color(is_selected ? theme.selected_foreground : theme.chrome),
                });
            } else {
                row_element = text(row) | color(is_selected ? theme.selected_foreground : theme.chrome);
            }
        } else if (pane_title.find("Watch List") != std::string::npos) {
            const auto watch_parts       = splitWatchRow(row);
            const auto watch_error_parts = splitWatchErrorRow(row);
            if (watch_error_parts.has_value()) {
                const auto& [expression, error] = watch_error_parts.value();
                const auto error_color          = is_selected ? theme.selected_watch_error : theme.watch_error;
                row_element                     = hbox({
                    text(expression) | color(error_color),
                    text(" ! ") | color(error_color),
                    text(error) | color(error_color),
                });
            } else if (watch_parts.has_value()) {
                const auto& [expression, middle, type, has_type] = watch_parts.value();
                if (has_type) {
                    row_element = hbox({
                        text(expression) | color(is_selected ? theme.selected_variable_name : theme.variable_name),
                        text(" = ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                        text(middle) | color(is_selected ? theme.selected_foreground : theme.chrome),
                        text(" : ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                        text(type) | color(is_selected ? theme.selected_variable_type : theme.variable_type),
                    });
                } else {
                    row_element = hbox({
                        text(expression) | color(is_selected ? theme.selected_variable_name : theme.variable_name),
                        text(" : ") | color(is_selected ? theme.selected_foreground : theme.chrome),
                        text(middle) | color(is_selected ? theme.selected_foreground : theme.chrome),
                    });
                }
            } else {
                row_element = text(row) | color(is_selected ? theme.selected_foreground : theme.chrome);
            }
        } else {
            row_element = text(row) | color(is_selected ? theme.selected_foreground : theme.chrome);
        }

        if (is_selected && !is_memory_pane) {
            row_element = row_element | bgcolor(theme.selected_background);
        }

        return row_element;
    }

    ftxui::Element renderSelectablePane(const SSelectablePaneState& pane, bool is_focused, const SAppTheme& theme, const SMemoryRenderContext& memory_context = {}) {
        using namespace ftxui;

        Elements rows;
        Element  title = text(pane.title) | bold | color(theme.title);
        if (is_focused) {
            title = title | bgcolor(theme.selected_background) | color(theme.selected_foreground);
        }

        rows.push_back(title);
        rows.push_back(separator());

        if (pane.rows.empty()) {
            rows.push_back(text("(empty)") | color(theme.chrome));
        } else {
            const bool is_memory_pane = pane.title.find("Memory") != std::string::npos;
            for (std::size_t i = 0; i < pane.rows.size(); ++i) {
                rows.push_back(renderPaneRow(pane.rows[i], i, is_focused && pane.selected_index == i, theme, pane.title, is_memory_pane, memory_context));
            }
        }

        return vbox(rows) | border | color(is_focused ? theme.accent : theme.chrome);
    }

    std::optional<eCommand> findCommandForKeys(const std::vector<SKeybinding>& keybindings, const std::string& keys) {
        const auto keybinding_iterator = std::ranges::find_if(keybindings, [&](const SKeybinding& keybinding) { return keybinding.keys == keys; });
        if (keybinding_iterator == keybindings.end()) {
            return std::nullopt;
        }

        return keybinding_iterator->command;
    }

    ftxui::Element renderShortcutsOverlay(const std::vector<SKeybinding>& keybindings, const SAppTheme& theme) {
        using namespace ftxui;

        Elements rows = {
            text(" Roundtable Shortcuts ") | bold | color(theme.title),
            separator(),
            text("Tab  Cycle focus") | color(theme.chrome),
            text("r  Refresh panes") | color(theme.chrome),
            text("F5  Continue") | color(theme.chrome),
            text("q  Quit") | color(theme.chrome),
        };

        for (const auto& keybinding : keybindings) {
            rows.push_back(hbox({
                text(keybinding.keys) | color(theme.hint_key),
                text("  "),
                text(commandDescription(keybinding.command)) | color(theme.hint_description),
            }));
        }

        return window(text(" Shortcuts ") | color(theme.title), vbox(rows)) | size(WIDTH, GREATER_THAN, 48) | color(theme.overlay_border);
    }

    ftxui::Element renderPromptOverlay(const SPromptState& prompt_state, const SAppTheme& theme) {
        using namespace ftxui;

        std::string title;
        std::string hint;
        switch (prompt_state.mode) {
            case ePromptMode::ADD_WATCH:
                title = " Add Watch ";
                hint  = "Enter expression and press Return";
                break;
            case ePromptMode::EDIT_WATCH:
                title = " Edit Watch ";
                hint  = "Update expression and press Return";
                break;
            case ePromptMode::ADD_BREAKPOINT:
                title = " Add Breakpoint ";
                hint  = "Enter source.cpp:line and press Return";
                break;
            case ePromptMode::MEMORY_TARGET:
                title = " Memory Target ";
                hint  = "Enter address or expression, empty clears override";
                break;
            case ePromptMode::NONE: return text("") | color(theme.chrome);
        }

        return window(text(title) | color(theme.title),
                      vbox({
                          text(hint) | color(theme.chrome),
                          separator(),
                          text(buildPromptDisplay(prompt_state)) | color(theme.accent),
                      })) |
            size(WIDTH, GREATER_THAN, 48) | color(theme.overlay_border);
    }

    ftxui::Element renderThemePickerOverlay(const SThemePickerState& theme_picker_state, const SAppTheme& theme) {
        using namespace ftxui;

        Elements rows = {
            text("j/k or arrows to preview") | color(theme.chrome),
            text("Return keep  Esc cancel") | color(theme.chrome),
            separator(),
        };

        for (std::size_t index = 0; index < kThemePresets.size(); ++index) {
            Element row = hbox({
                              text(index == theme_picker_state.selected_index ? "> " : "  "),
                              text(themePresetName(kThemePresets[index])),
                          }) |
                color(theme.chrome);
            if (index == theme_picker_state.selected_index) {
                row = row | bgcolor(theme.selected_background) | color(theme.selected_foreground);
            }
            rows.push_back(row);
        }

        return window(text(" Theme Picker ") | color(theme.title), vbox(rows)) | size(WIDTH, EQUAL, 30) | color(theme.overlay_border);
    }

    ftxui::Element renderProfilePickerOverlay(const SProfilePickerState& profile_picker_state, const std::vector<SLaunchProfileConfig>& profiles, const SAppTheme& theme) {
        using namespace ftxui;

        Elements rows = {
            text("j/k or arrows to select") | color(theme.chrome),
            text("Return restart  Esc cancel") | color(theme.chrome),
            separator(),
        };

        for (std::size_t index = 0; index < profiles.size(); ++index) {
            Element row = hbox({
                              text(index == profile_picker_state.selected_index ? "> " : "  "),
                              text(profiles[index].name),
                          }) |
                color(theme.chrome);
            if (index == profile_picker_state.selected_index) {
                row = row | bgcolor(theme.selected_background) | color(theme.selected_foreground);
            }
            rows.push_back(row);
        }

        if (profiles.empty()) {
            rows.push_back(text("No launch profiles configured") | color(theme.chrome));
        }

        return window(text(" Launch Profile ") | color(theme.title), vbox(rows)) | size(WIDTH, EQUAL, 36) | color(theme.overlay_border);
    }

    ftxui::Element renderLeaderPopup(const std::vector<SKeybinding>& keybindings, eFocusPane focused_pane, const SAppTheme& theme) {
        using namespace ftxui;

        const auto hints = buildLeaderHintRows(keybindings, focused_pane);
        Elements   left_column;
        Elements   right_column;
        left_column.push_back(text(" Pane-first ") | bold | color(theme.title));
        right_column.push_back(text(" Global ") | bold | color(theme.title));

        for (const auto& hint : hints) {
            Elements& target_column = hint.pane_specific ? left_column : right_column;
            target_column.push_back(hbox({
                text(hint.keys) | color(hint.pane_specific ? theme.hint_specific_key : theme.hint_key),
                text("  "),
                text(hint.description) | color(hint.pane_specific ? theme.hint_specific_text : theme.hint_description),
            }));
        }

        return window(text(" Leader ") | color(theme.title),
                      hbox({
                          vbox(left_column) | flex,
                          separator(),
                          vbox(right_column) | flex,
                      })) |
            size(WIDTH, GREATER_THAN, 58) | color(theme.overlay_border);
    }

    ftxui::Element renderAuxiliaryViews(const SViewVisibilityState& view_visibility, const SSelectablePaneState& memory_view_pane, const SSelectablePaneState& disassembly_pane,
                                        eFocusPane focused_pane, const SAppTheme& theme, const SMemoryRenderContext& memory_context) {
        using namespace ftxui;

        const bool show_memory       = view_visibility.show_memory_view;
        const bool show_disassembly  = view_visibility.show_disassembly_view;
        const bool memory_is_focused = focused_pane == eFocusPane::MEMORY_VIEW;
        const bool disasm_is_focused = focused_pane == eFocusPane::DISASSEMBLY_VIEW;

        if (show_memory && show_disassembly) {
            return hbox({
                       renderSelectablePane(memory_view_pane, memory_is_focused, theme, memory_context) | flex,
                       renderSelectablePane(disassembly_pane, disasm_is_focused, theme) | flex,
                   }) |
                flex;
        }

        if (show_memory) {
            return renderSelectablePane(memory_view_pane, memory_is_focused, theme, memory_context) | flex;
        }

        if (show_disassembly) {
            return renderSelectablePane(disassembly_pane, disasm_is_focused, theme) | flex;
        }

        return renderSelectablePane(
                   {
                       .title = " Views ",
                       .rows  = {"Enable Memory or Disassembly with Space t / Space a"},
                   },
                   false, theme) |
            flex;
    }

} // namespace

int main(int argc, char** argv) {
    using namespace ftxui;

    const SAppStartupResult startup_result = initializeAppStartup(argc, argv, std::cout);
    if (startup_result.should_exit) {
        return startup_result.exit_code;
    }

    const SCliOptions              cli_options                  = startup_result.cli_options;
    const std::string              config_path                  = startup_result.config_path;
    SAppConfig                     app_config                   = startup_result.app_config;
    const auto                     buildActiveTheme             = [&](eThemePreset preset) { return applyThemeOverrides(buildTheme(preset), app_config.theme_overrides); };
    eThemePreset                   active_theme_preset          = app_config.theme_preset;
    SAppTheme                      app_theme                    = buildActiveTheme(active_theme_preset);
    SSessionBootstrapResult        bootstrap_result             = bootstrapSession(app_config);
    std::unique_ptr<IDebugSession> debug_session                = std::move(bootstrap_result.session);
    SDebugSelection                debug_selection              = bootstrap_result.selection;
    std::uint64_t                  disassembly_start_address    = bootstrap_result.disassembly_start_address;
    std::string                    disassembly_memory_reference = bootstrap_result.disassembly_memory_reference;
    SStoppedContext                stopped_context              = bootstrap_result.stopped_context;
    eDebuggerSessionState          session_state                = bootstrap_result.state;
    std::string                    base_session_status          = bootstrap_result.status_message;
    std::string                    transient_status_message     = {};
    std::int64_t                   memory_navigation_offset     = 0;
    auto                           screen                       = ScreenInteractive::Fullscreen();
    SViewVisibilityState           view_visibility              = {
                               .show_memory_view      = app_config.show_memory_view,
                               .show_disassembly_view = app_config.show_disassembly_view,
    };
    eFocusPane               focused_pane                 = normalizeFocusedPane(app_config.startup_focus, view_visibility);
    std::vector<SKeybinding> keybindings                  = app_config.keybindings;
    bool                     leader_pending               = false;
    const auto               buildInitialWatchExpressions = [](const SAppConfig& config) {
        if (!config.watches.empty()) {
            std::vector<SWatchExpression> configured_watches;
            configured_watches.reserve(config.watches.size());
            for (const auto& watch : config.watches) {
                configured_watches.push_back({.expression = watch});
            }
            return configured_watches;
        }

        if (config.session_mode == eSessionMode::MOCK) {
            return std::vector<SWatchExpression>{
                {.expression = "a"},
                {.expression = "ptr"},
            };
        }

        return std::vector<SWatchExpression>{
            {.expression = "sample_value"},
            {.expression = "sample_bytes"},
        };
    };
    std::vector<SWatchExpression> watch_expressions    = buildInitialWatchExpressions(app_config);
    std::string                   manual_memory_target = {};
    SPromptState                  prompt_state         = {};
    SThemePickerState             theme_picker_state   = {
                      .active          = false,
                      .original_preset = active_theme_preset,
                      .selected_index  = themePresetIndex(active_theme_preset),
    };
    SProfilePickerState   profile_picker_state    = {};
    eWatchActionMode      watch_action_mode       = eWatchActionMode::NONE;
    eBreakpointActionMode breakpoint_action_mode  = eBreakpointActionMode::NONE;
    SAsyncDapControlState async_dap_control_state = {};

    SSelectablePaneState  locals_pane = {
         .title = " Locals ",
         .rows  = {},
    };
    SSelectablePaneState threads_pane = {
        .title = " Threads ",
        .rows  = {},
    };
    SSelectablePaneState stack_pane = {
        .title = " Stack ",
        .rows  = {},
    };
    SSelectablePaneState memory_view_pane = {
        .title = " Memory View ",
        .rows  = {},
    };
    SSelectablePaneState disassembly_pane = {
        .title = " Disassembly ",
        .rows  = {},
    };
    SSelectablePaneState watch_list_pane = {
        .title = " Watch List ",
        .rows  = {},
    };
    SSelectablePaneState breakpoints_pane = {
        .title = " Breakpoints ",
        .rows  = {},
    };
    std::string          memory_target_label = {};
    SMemoryRenderContext memory_context      = {};

    const auto           isDapControlRunning = [&] { return async_dap_control_state.running.load(); };

    const auto           clearDebuggerPanes = [&] {
        locals_pane.rows                = {"Session terminated"};
        threads_pane.rows               = {"Session terminated"};
        stack_pane.rows                 = {"Session terminated"};
        memory_view_pane.rows           = {"Session terminated"};
        disassembly_pane.rows           = {"Session terminated"};
        watch_list_pane.rows            = {"Session terminated"};
        breakpoints_pane.rows           = {"Session terminated"};
        locals_pane.selected_index      = 0;
        threads_pane.selected_index     = 0;
        stack_pane.selected_index       = 0;
        memory_view_pane.selected_index = 0;
        disassembly_pane.selected_index = 0;
        watch_list_pane.selected_index  = 0;
        breakpoints_pane.selected_index = 0;
        memory_target_label             = {};
        memory_context                  = {};
        stopped_context                 = {};
    };

    const auto isSessionTerminated = [&] { return session_state == eDebuggerSessionState::TERMINATED; };

    const auto refreshAllPanes = [&] {
        if (isDapControlRunning()) {
            return;
        }
        if (isSessionTerminated()) {
            clearDebuggerPanes();
            return;
        }

        threads_pane.title = " Threads [" + std::to_string(stopped_context.threads.size()) + "] ";
        threads_pane.rows  = formatThreadPaneRows(stopped_context.threads);
        threads_pane.selected_index =
            std::min(selectedThreadIndex(stopped_context.threads, debug_selection.thread_id), threads_pane.rows.empty() ? 0UL : threads_pane.rows.size() - 1);
        stack_pane.title                = " Stack [T:" + std::to_string(debug_selection.thread_id) + "] ";
        stack_pane.rows                 = formatStackPaneRows(stopped_context.stack_frames);
        stack_pane.selected_index       = std::min(debug_selection.frame_index, stack_pane.rows.empty() ? 0UL : stack_pane.rows.size() - 1);
        breakpoints_pane.title          = " Breakpoints [" + std::to_string(app_config.breakpoints.size()) + "] ";
        breakpoints_pane.rows           = formatBreakpointPaneRows(app_config.breakpoints);
        breakpoints_pane.selected_index = std::min(breakpoints_pane.selected_index, breakpoints_pane.rows.empty() ? 0UL : breakpoints_pane.rows.size() - 1);

        SPaneRefreshInputs inputs = {
            .debug_session                = *debug_session,
            .debug_selection              = debug_selection,
            .disassembly_start_address    = disassembly_start_address,
            .disassembly_memory_reference = disassembly_memory_reference,
            .memory_navigation_offset     = memory_navigation_offset,
            .focused_pane                 = focused_pane,
            .watch_expressions            = watch_expressions,
            .manual_memory_target         = manual_memory_target,
        };
        SPaneRefreshOutputs outputs = {
            .locals_pane         = locals_pane,
            .memory_view_pane    = memory_view_pane,
            .disassembly_pane    = disassembly_pane,
            .watch_list_pane     = watch_list_pane,
            .memory_target_label = memory_target_label,
            .memory_context      = memory_context,
        };
        refreshPaneRows(inputs, outputs);
    };

    const auto applyBootstrapResult = [&](SSessionBootstrapResult new_bootstrap_result, std::string status_message) {
        bootstrap_result             = std::move(new_bootstrap_result);
        debug_session                = std::move(bootstrap_result.session);
        debug_selection              = bootstrap_result.selection;
        disassembly_start_address    = bootstrap_result.disassembly_start_address;
        disassembly_memory_reference = bootstrap_result.disassembly_memory_reference;
        stopped_context              = bootstrap_result.stopped_context;
        session_state                = bootstrap_result.state;
        base_session_status          = bootstrap_result.status_message;
        transient_status_message     = std::move(status_message);
        memory_navigation_offset     = 0;
    };

    const auto clearBreakpointSourceInActiveSession = [&](const std::filesystem::path& source_path) {
        if (session_state != eDebuggerSessionState::STOPPED) {
            return true;
        }

        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr || source_path.empty()) {
            return true;
        }

        const auto response = dap_session->setBreakpoints({
            .source_path = std::filesystem::absolute(source_path).string(),
            .breakpoints = {},
        });
        if (!response.success) {
            transient_status_message = "DAP clear breakpoints failed: " + response.error_message;
            return false;
        }

        return true;
    };

    const auto hasBreakpointForSource = [&](const std::filesystem::path& source_path) {
        if (source_path.empty()) {
            return false;
        }

        const auto absolute_source_path = std::filesystem::absolute(source_path);
        return std::ranges::any_of(app_config.breakpoints, [&](const SSourceBreakpointConfig& breakpoint) {
            return !breakpoint.source_path.empty() && std::filesystem::absolute(breakpoint.source_path) == absolute_source_path;
        });
    };

    const auto applyBreakpointsToActiveSession = [&]() {
        if (isDapControlRunning()) {
            transient_status_message = "Breakpoints cannot be changed while debugger is running";
            return;
        }
        if (session_state == eDebuggerSessionState::TERMINATED || session_state == eDebuggerSessionState::ERROR) {
            transient_status_message = "Breakpoint queued for next DAP launch";
            return;
        }

        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr) {
            transient_status_message = "Breakpoint queued for next DAP launch";
            return;
        }

        std::string breakpoint_error_message;
        if (!configureDapBreakpoints(*dap_session, app_config.breakpoints, breakpoint_error_message)) {
            transient_status_message = breakpoint_error_message;
            return;
        }

        transient_status_message = "Breakpoint applied";
    };

    const auto moveSelectedWatchUp = [&]() {
        if (watch_list_pane.selected_index == 0 || watch_list_pane.selected_index >= watch_expressions.size()) {
            transient_status_message = "Watch is already at the top";
            return;
        }

        std::swap(watch_expressions[watch_list_pane.selected_index], watch_expressions[watch_list_pane.selected_index - 1]);
        --watch_list_pane.selected_index;
        memory_navigation_offset = 0;
        transient_status_message = "Watch moved up";
        refreshAllPanes();
    };

    const auto moveSelectedWatchDown = [&]() {
        if (watch_list_pane.selected_index >= watch_expressions.size() || watch_list_pane.selected_index + 1 >= watch_expressions.size()) {
            transient_status_message = "Watch is already at the bottom";
            return;
        }

        std::swap(watch_expressions[watch_list_pane.selected_index], watch_expressions[watch_list_pane.selected_index + 1]);
        ++watch_list_pane.selected_index;
        memory_navigation_offset = 0;
        transient_status_message = "Watch moved down";
        refreshAllPanes();
    };

    const auto duplicateSelectedWatch = [&]() {
        if (watch_list_pane.selected_index >= watch_expressions.size()) {
            transient_status_message = "No watch selected";
            return;
        }

        const auto insert_position = watch_expressions.begin() + static_cast<std::ptrdiff_t>(watch_list_pane.selected_index + 1);
        watch_expressions.insert(insert_position, watch_expressions[watch_list_pane.selected_index]);
        ++watch_list_pane.selected_index;
        memory_navigation_offset = 0;
        transient_status_message = "Watch duplicated";
        refreshAllPanes();
    };

    const auto performWatchAction = [&](eWatchActionMode action_mode) {
        if (watch_expressions.empty()) {
            transient_status_message = "No watch entries";
            return;
        }

        switch (action_mode) {
            case eWatchActionMode::NONE: break;
            case eWatchActionMode::EDIT:
                if (watch_list_pane.selected_index < watch_expressions.size()) {
                    prompt_state = beginPrompt(ePromptMode::EDIT_WATCH, watch_expressions[watch_list_pane.selected_index].expression, true);
                }
                break;
            case eWatchActionMode::REMOVE:
                if (watch_list_pane.selected_index < watch_expressions.size()) {
                    watch_expressions.erase(watch_expressions.begin() + static_cast<std::ptrdiff_t>(watch_list_pane.selected_index));
                    if (watch_list_pane.selected_index > 0 && watch_list_pane.selected_index >= watch_expressions.size()) {
                        --watch_list_pane.selected_index;
                    }
                    memory_navigation_offset = 0;
                    transient_status_message = "Watch removed";
                    refreshAllPanes();
                }
                break;
            case eWatchActionMode::MOVE_UP: moveSelectedWatchUp(); break;
            case eWatchActionMode::MOVE_DOWN: moveSelectedWatchDown(); break;
            case eWatchActionMode::DUPLICATE: duplicateSelectedWatch(); break;
        }
    };

    const auto chooseOrPerformWatchAction = [&](eWatchActionMode action_mode, std::string choose_message) {
        if (watch_expressions.empty()) {
            transient_status_message = "No watch entries";
            return;
        }

        if (focused_pane != eFocusPane::WATCH_LIST) {
            focused_pane             = eFocusPane::WATCH_LIST;
            watch_action_mode        = action_mode;
            transient_status_message = std::move(choose_message);
            refreshAllPanes();
            return;
        }

        performWatchAction(action_mode);
    };

    const auto reloadConfig = [&] {
        if (isDapControlRunning() || session_state == eDebuggerSessionState::RUNNING || session_state == eDebuggerSessionState::LAUNCHING ||
            session_state == eDebuggerSessionState::TERMINATING) {
            transient_status_message = "Config reload is disabled while debugger is running";
            return;
        }

        const SAppConfig previous_config      = app_config;
        const auto       reload_config_result = loadAppConfigForCliWithDiagnostics(config_path, cli_options.config_path_explicit);
        SAppConfig       reloaded_config      = reload_config_result.config;
        applyCliOverrides(cli_options, reloaded_config);

        app_config  = std::move(reloaded_config);
        keybindings = app_config.keybindings;
        if (!app_config.watches.empty() || session_state != eDebuggerSessionState::STOPPED) {
            watch_expressions = buildInitialWatchExpressions(app_config);
        }
        active_theme_preset                   = app_config.theme_preset;
        app_theme                             = buildActiveTheme(active_theme_preset);
        view_visibility.show_memory_view      = app_config.show_memory_view;
        view_visibility.show_disassembly_view = app_config.show_disassembly_view;
        focused_pane                          = normalizeFocusedPane(focused_pane, view_visibility);

        theme_picker_state = {
            .active          = false,
            .original_preset = active_theme_preset,
            .selected_index  = themePresetIndex(active_theme_preset),
        };
        profile_picker_state = {};

        if (session_state == eDebuggerSessionState::STOPPED) {
            applyBreakpointsToActiveSession();
            for (const auto& previous_breakpoint : previous_config.breakpoints) {
                if (!hasBreakpointForSource(previous_breakpoint.source_path)) {
                    clearBreakpointSourceInActiveSession(previous_breakpoint.source_path);
                }
            }

            if (auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get()); dap_session != nullptr) {
                stopped_context = updateDapStoppedContext(*dap_session, debug_selection, disassembly_start_address, disassembly_memory_reference);
            }

            const bool launch_settings_changed = previous_config.session_mode != app_config.session_mode || previous_config.dap_launch.command != app_config.dap_launch.command ||
                previous_config.dap_launch.liblldb_path != app_config.dap_launch.liblldb_path || previous_config.dap_launch.program != app_config.dap_launch.program ||
                previous_config.dap_launch.arguments != app_config.dap_launch.arguments ||
                previous_config.dap_launch.working_directory != app_config.dap_launch.working_directory ||
                previous_config.dap_launch.stop_on_entry != app_config.dap_launch.stop_on_entry || previous_config.dap_launch.continue_once != app_config.dap_launch.continue_once;
            transient_status_message = launch_settings_changed ? "Config reloaded; launch changes apply on restart" : "Config reloaded";
        } else {
            applyBootstrapResult(bootstrapSession(app_config), "Config reloaded");
        }

        if (!reload_config_result.diagnostics.empty()) {
            transient_status_message = "Config reload warning: " + reload_config_result.diagnostics.front();
            if (reload_config_result.diagnostics.size() > 1) {
                transient_status_message += " (+" + std::to_string(reload_config_result.diagnostics.size() - 1) + " more)";
            }
        }

        memory_navigation_offset = 0;
        refreshAllPanes();
    };

    const auto executeDapControl = [&](const std::string& action_name, const std::string& stopped_action_name, auto send_request) {
        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr) {
            transient_status_message = action_name + " is only available in DAP sessions";
            return;
        }
        if (session_state == eDebuggerSessionState::TERMINATED) {
            transient_status_message = action_name + " is unavailable after session termination";
            return;
        }
        if (session_state == eDebuggerSessionState::ERROR) {
            transient_status_message = action_name + " is unavailable because the session failed to start";
            return;
        }
        if (isDapControlRunning()) {
            transient_status_message = "Debugger is already running";
            return;
        }
        if (debug_selection.thread_id == 0) {
            transient_status_message = action_name + " failed: no active thread";
            return;
        }

        const int thread_id = static_cast<int>(debug_selection.thread_id);
        session_state       = eDebuggerSessionState::RUNNING;
        async_dap_control_state.running.store(true);
        async_dap_control_state.pause_requested.store(false);
        async_dap_control_state.terminate_requested.store(false);
        transient_status_message = "Running: " + stopped_action_name;

        async_dap_control_state.worker = std::jthread([&, dap_session, action_name, stopped_action_name, send_request, thread_id](std::stop_token) mutable {
            bool        success = false;
            std::string error_message;

            bool        terminated = false;

            if (!send_request(*dap_session, thread_id)) {
                error_message = action_name + " failed: " + dap_session->getLastError();
            } else if (!dap_session->waitForStoppedEvent()) {
                terminated = async_dap_control_state.terminate_requested.load() && dap_session->getLastError() == "DAP session ended before a stopped event";
                if (!terminated) {
                    error_message = "Wait after " + stopped_action_name + " failed: " + dap_session->getLastError();
                }
            } else {
                success = true;
            }

            screen.Post(Closure([&, dap_session, success, terminated, error_message, stopped_action_name] {
                async_dap_control_state.running.store(false);
                async_dap_control_state.terminate_requested.store(false);
                if (terminated) {
                    async_dap_control_state.pause_requested.store(false);
                    session_state            = eDebuggerSessionState::TERMINATED;
                    base_session_status      = "Session terminated";
                    transient_status_message = "Terminated";
                    clearDebuggerPanes();
                    if (async_dap_control_state.quit_requested.exchange(false)) {
                        screen.Exit();
                    }
                    return;
                }
                if (!success) {
                    async_dap_control_state.pause_requested.store(false);
                    session_state            = eDebuggerSessionState::ERROR;
                    transient_status_message = error_message;
                    return;
                }

                stopped_context = updateDapStoppedContext(*dap_session, debug_selection, disassembly_start_address, disassembly_memory_reference);

                const std::string completed_action_name = async_dap_control_state.pause_requested.exchange(false) ? "pause" : stopped_action_name;
                async_dap_control_state.quit_requested.store(false);
                session_state            = eDebuggerSessionState::STOPPED;
                memory_navigation_offset = 0;
                transient_status_message = "Stopped after " + completed_action_name;
                refreshAllPanes();
            }));
            screen.PostEvent(Event::Custom);
        });
    };

    const auto continueActiveDapSession = [&]() {
        executeDapControl("Continue", "continue", [](CDapDebugSession& dap_session, int thread_id) {
            return dap_session.sendContinueRequest({
                .thread_id = thread_id,
            });
        });
    };

    const auto stepOverActiveDapSession = [&]() {
        executeDapControl("Step over", "step over", [](CDapDebugSession& dap_session, int thread_id) {
            return dap_session.sendStepOverRequest({
                .thread_id = thread_id,
            });
        });
    };

    const auto stepIntoActiveDapSession = [&]() {
        executeDapControl("Step into", "step into", [](CDapDebugSession& dap_session, int thread_id) {
            return dap_session.sendStepIntoRequest({
                .thread_id = thread_id,
            });
        });
    };

    const auto stepOutActiveDapSession = [&]() {
        executeDapControl("Step out", "step out", [](CDapDebugSession& dap_session, int thread_id) {
            return dap_session.sendStepOutRequest({
                .thread_id = thread_id,
            });
        });
    };

    const auto pauseActiveDapSession = [&]() {
        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr) {
            transient_status_message = "Pause is only available in DAP sessions";
            return;
        }
        if (!isDapControlRunning()) {
            transient_status_message = "Pause is only available while debugger is running";
            return;
        }
        if (debug_selection.thread_id == 0) {
            transient_status_message = "Pause failed: no active thread";
            return;
        }

        if (!dap_session->sendPauseRequest({
                .thread_id = static_cast<int>(debug_selection.thread_id),
            })) {
            transient_status_message = "Pause failed: " + dap_session->getLastError();
            return;
        }

        async_dap_control_state.pause_requested.store(true);
        transient_status_message = "Pause requested";
    };

    const auto disconnectActiveDapSession = [&](bool exit_after_disconnect) {
        auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
        if (dap_session == nullptr) {
            if (exit_after_disconnect) {
                screen.Exit();
                return;
            }
            transient_status_message = "Terminate is only available in DAP sessions";
            return;
        }
        if (session_state == eDebuggerSessionState::TERMINATED || session_state == eDebuggerSessionState::ERROR) {
            if (exit_after_disconnect) {
                screen.Exit();
                return;
            }
            transient_status_message = "Session is already terminated";
            return;
        }
        if (session_state == eDebuggerSessionState::TERMINATING) {
            async_dap_control_state.quit_requested.store(exit_after_disconnect || async_dap_control_state.quit_requested.load());
            transient_status_message = "Terminate already requested";
            return;
        }
        if (isDapControlRunning()) {
            if (!dap_session->sendDisconnectRequest({.terminate_debuggee = true})) {
                transient_status_message = "Terminate failed: " + dap_session->getLastError();
                return;
            }

            session_state = eDebuggerSessionState::TERMINATING;
            async_dap_control_state.terminate_requested.store(true);
            async_dap_control_state.pause_requested.store(false);
            async_dap_control_state.quit_requested.store(exit_after_disconnect);
            transient_status_message = "Terminate requested";
            return;
        }

        session_state = eDebuggerSessionState::TERMINATING;
        async_dap_control_state.running.store(true);
        async_dap_control_state.pause_requested.store(false);
        async_dap_control_state.terminate_requested.store(true);
        async_dap_control_state.quit_requested.store(exit_after_disconnect);
        transient_status_message = "Terminate requested";

        async_dap_control_state.worker = std::jthread([&, dap_session](std::stop_token) {
            bool        success = false;
            std::string error_message;

            if (!dap_session->sendDisconnectRequest({.terminate_debuggee = true})) {
                error_message = "Terminate failed: " + dap_session->getLastError();
            } else if (!dap_session->waitForTerminatedEvent()) {
                error_message = "Wait after terminate failed: " + dap_session->getLastError();
            } else {
                success = true;
            }

            screen.Post(Closure([&, success, error_message] {
                async_dap_control_state.running.store(false);
                async_dap_control_state.pause_requested.store(false);
                async_dap_control_state.terminate_requested.store(false);
                if (!success) {
                    async_dap_control_state.quit_requested.store(false);
                    session_state            = eDebuggerSessionState::ERROR;
                    transient_status_message = error_message;
                    return;
                }

                session_state            = eDebuggerSessionState::TERMINATED;
                base_session_status      = "Session terminated";
                transient_status_message = "Terminated";
                clearDebuggerPanes();
                if (async_dap_control_state.quit_requested.exchange(false)) {
                    screen.Exit();
                }
            }));
            screen.PostEvent(Event::Custom);
        });
    };

    const auto terminateActiveDapSession = [&]() { disconnectActiveDapSession(false); };

    const auto quitApplication = [&]() {
        if (session_state == eDebuggerSessionState::TERMINATING) {
            async_dap_control_state.quit_requested.store(true);
            transient_status_message = "Waiting for debugger disconnect before quitting";
            return;
        }
        if (isDapControlRunning() || session_state == eDebuggerSessionState::STOPPED || session_state == eDebuggerSessionState::RUNNING) {
            disconnectActiveDapSession(true);
            return;
        }

        screen.Exit();
    };

    const auto restartSession = [&]() {
        if (isDapControlRunning() || session_state == eDebuggerSessionState::RUNNING || session_state == eDebuggerSessionState::TERMINATING) {
            transient_status_message = "Restart is unavailable while debugger is running";
            return;
        }

        if (app_config.session_mode == eSessionMode::MOCK) {
            applyBootstrapResult(bootstrapSession(app_config), "Mock session restarted");
            refreshAllPanes();
            return;
        }

        if (session_state == eDebuggerSessionState::STOPPED) {
            auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get());
            if (dap_session != nullptr) {
                if (!dap_session->sendDisconnectRequest({.terminate_debuggee = true})) {
                    session_state            = eDebuggerSessionState::ERROR;
                    transient_status_message = "Restart failed: " + dap_session->getLastError();
                    return;
                }

                if (!dap_session->waitForTerminatedEvent()) {
                    session_state            = eDebuggerSessionState::ERROR;
                    transient_status_message = "Restart failed while stopping current session: " + dap_session->getLastError();
                    return;
                }
            }
        }

        applyBootstrapResult(bootstrapSession(app_config), "Session restarted");
        if (session_state == eDebuggerSessionState::ERROR) {
            transient_status_message = "Restart failed: " + base_session_status;
        }
        refreshAllPanes();
    };

    refreshAllPanes();

    auto renderer = Renderer([&] {
        const std::string current_status         = transient_status_message.empty() ? base_session_status : transient_status_message;
        const std::string stopped_context_status = formatStoppedContextStatus(stopped_context, debug_selection);
        Element           locals                 = renderSelectablePane(locals_pane, focused_pane == eFocusPane::LOCALS, app_theme);
        Element           threads                = renderSelectablePane(threads_pane, focused_pane == eFocusPane::THREADS, app_theme);
        Element           stack                  = renderSelectablePane(stack_pane, focused_pane == eFocusPane::STACK, app_theme);
        Element           watch_list             = renderSelectablePane(watch_list_pane, focused_pane == eFocusPane::WATCH_LIST, app_theme);
        Element           breakpoints            = renderSelectablePane(breakpoints_pane, focused_pane == eFocusPane::BREAKPOINTS, app_theme);
        Element           auxiliary_views        = renderAuxiliaryViews(view_visibility, memory_view_pane, disassembly_pane, focused_pane, app_theme, memory_context);
        Element           left_column            = vbox({
            locals | flex,
            threads | size(HEIGHT, EQUAL, 6),
            stack | size(HEIGHT, EQUAL, 8),
        });
        Element           right_column           = vbox({
            watch_list | flex,
            breakpoints | size(HEIGHT, EQUAL, 8),
        });

        Elements          runtime_status_items = {
            text(" Roundtable ") | bgcolor(app_theme.selected_background) | color(app_theme.selected_foreground),
            separator(),
            text(" " + current_status + " ") | color(app_theme.chrome),
        };

        Elements frame_status_items = {
            text(" " + stopped_context_status + " ") | color(app_theme.chrome),
        };

        Elements command_status_items = {
            text(" Tab cycle ") | color(app_theme.chrome),      separator(), text(" r refresh ") | color(app_theme.chrome), separator(),
            text(" Space commands ") | color(app_theme.accent), separator(), text(" q quit ") | color(app_theme.chrome),
        };

        Element status_bar = vbox({
                                 hbox(runtime_status_items),
                                 separator(),
                                 hbox(frame_status_items),
                                 separator(),
                                 hbox(command_status_items),
                             }) |
            border | color(app_theme.chrome);

        Element content = vbox({
            hbox({
                left_column | size(WIDTH, EQUAL, 28),
                auxiliary_views | flex,
                right_column | size(WIDTH, EQUAL, 28),
            }) | flex,
            status_bar,
        });

        if (view_visibility.show_shortcuts_overlay) {
            content = dbox({
                content,
                renderShortcutsOverlay(keybindings, app_theme) | center,
            });
        }

        if (leader_pending) {
            content = dbox({
                content,
                vbox({
                    filler(),
                    renderLeaderPopup(keybindings, focused_pane, app_theme) | center,
                    filler() | size(HEIGHT, EQUAL, 5),
                }),
            });
        }

        if (theme_picker_state.active) {
            content = dbox({
                content,
                renderThemePickerOverlay(theme_picker_state, app_theme) | center,
            });
        }

        if (profile_picker_state.active) {
            content = dbox({
                content,
                renderProfilePickerOverlay(profile_picker_state, app_config.launch_profiles, app_theme) | center,
            });
        }

        if (prompt_state.mode != ePromptMode::NONE) {
            content = dbox({
                content,
                renderPromptOverlay(prompt_state, app_theme) | center,
            });
        }

        return content;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (prompt_state.mode != ePromptMode::NONE) {
            if (event == Event::Escape) {
                prompt_state = {};
                return true;
            }

            if (event == Event::Return) {
                if (prompt_state.mode == ePromptMode::ADD_WATCH) {
                    if (!prompt_state.input.empty()) {
                        watch_expressions.push_back({.expression = prompt_state.input});
                        focused_pane             = eFocusPane::WATCH_LIST;
                        memory_navigation_offset = 0;
                    }
                } else if (prompt_state.mode == ePromptMode::EDIT_WATCH) {
                    if (!watch_expressions.empty() && watch_list_pane.selected_index < watch_expressions.size() && !prompt_state.input.empty()) {
                        watch_expressions[watch_list_pane.selected_index].expression = prompt_state.input;
                        focused_pane                                                 = eFocusPane::WATCH_LIST;
                        memory_navigation_offset                                     = 0;
                    }
                } else if (prompt_state.mode == ePromptMode::ADD_BREAKPOINT) {
                    const auto breakpoint = parseSourceBreakpointConfig(prompt_state.input);
                    if (breakpoint.has_value()) {
                        app_config.breakpoints.push_back(breakpoint.value());
                        breakpoints_pane.selected_index = app_config.breakpoints.empty() ? 0UL : app_config.breakpoints.size() - 1;
                        focused_pane                    = eFocusPane::BREAKPOINTS;
                        applyBreakpointsToActiveSession();
                    } else {
                        transient_status_message = "Invalid breakpoint, expected source.cpp:line";
                    }
                } else if (prompt_state.mode == ePromptMode::MEMORY_TARGET) {
                    manual_memory_target     = prompt_state.input;
                    focused_pane             = eFocusPane::MEMORY_VIEW;
                    memory_navigation_offset = 0;
                }

                prompt_state = {};
                refreshAllPanes();
                return true;
            }

            if (event == Event::ArrowLeft) {
                if (prompt_state.replace_on_input) {
                    prompt_state.cursor_index     = 0;
                    prompt_state.replace_on_input = false;
                } else if (prompt_state.cursor_index > 0) {
                    --prompt_state.cursor_index;
                }
                return true;
            }

            if (event == Event::ArrowRight) {
                if (prompt_state.replace_on_input) {
                    prompt_state.cursor_index     = prompt_state.input.size();
                    prompt_state.replace_on_input = false;
                } else if (prompt_state.cursor_index < prompt_state.input.size()) {
                    ++prompt_state.cursor_index;
                }
                return true;
            }

            if (event == Event::Home) {
                prompt_state.cursor_index     = 0;
                prompt_state.replace_on_input = false;
                return true;
            }

            if (event == Event::End) {
                prompt_state.cursor_index     = prompt_state.input.size();
                prompt_state.replace_on_input = false;
                return true;
            }

            if (event == Event::Backspace) {
                if (prompt_state.replace_on_input) {
                    prompt_state.cursor_index     = 0;
                    prompt_state.replace_on_input = false;
                    prompt_state.input.clear();
                } else if (prompt_state.cursor_index > 0 && !prompt_state.input.empty()) {
                    prompt_state.input.erase(prompt_state.cursor_index - 1, 1);
                    --prompt_state.cursor_index;
                }
                return true;
            }

            if (event == Event::Delete) {
                if (prompt_state.replace_on_input) {
                    prompt_state.cursor_index     = prompt_state.input.size();
                    prompt_state.replace_on_input = false;
                    prompt_state.input.clear();
                } else if (prompt_state.cursor_index < prompt_state.input.size()) {
                    prompt_state.input.erase(prompt_state.cursor_index, 1);
                }
                return true;
            }

            if (event.is_character()) {
                if (prompt_state.replace_on_input) {
                    prompt_state.input            = event.character();
                    prompt_state.cursor_index     = prompt_state.input.size();
                    prompt_state.replace_on_input = false;
                } else {
                    prompt_state.input.insert(prompt_state.cursor_index, event.character());
                    prompt_state.cursor_index += event.character().size();
                }
                return true;
            }

            return true;
        }

        if (theme_picker_state.active) {
            if (event == Event::Escape) {
                active_theme_preset      = theme_picker_state.original_preset;
                app_theme                = buildActiveTheme(active_theme_preset);
                theme_picker_state       = {};
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                theme_picker_state.active = false;
                transient_status_message  = {};
                return true;
            }

            const auto move_up   = event == Event::ArrowUp || event == Event::Character('k');
            const auto move_down = event == Event::ArrowDown || event == Event::Character('j');

            if (move_up && theme_picker_state.selected_index > 0) {
                --theme_picker_state.selected_index;
            } else if (move_down && theme_picker_state.selected_index + 1 < kThemePresets.size()) {
                ++theme_picker_state.selected_index;
            } else if (!(move_up || move_down)) {
                return true;
            }

            active_theme_preset      = kThemePresets[theme_picker_state.selected_index];
            app_theme                = buildActiveTheme(active_theme_preset);
            transient_status_message = "Theme preview: " + themePresetName(active_theme_preset);
            return true;
        }

        if (profile_picker_state.active) {
            if (event == Event::Escape) {
                profile_picker_state     = {};
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                if (profile_picker_state.selected_index >= app_config.launch_profiles.size()) {
                    profile_picker_state     = {};
                    transient_status_message = "No launch profiles configured";
                    return true;
                }

                const std::string profile_name = app_config.launch_profiles[profile_picker_state.selected_index].name;
                profile_picker_state           = {};
                if (!applyLaunchProfile(app_config, profile_name)) {
                    transient_status_message = "Launch profile not found: " + profile_name;
                    return true;
                }

                restartSession();
                return true;
            }

            const auto move_up   = event == Event::ArrowUp || event == Event::Character('k');
            const auto move_down = event == Event::ArrowDown || event == Event::Character('j');

            if (move_up && profile_picker_state.selected_index > 0) {
                --profile_picker_state.selected_index;
            } else if (move_down && profile_picker_state.selected_index + 1 < app_config.launch_profiles.size()) {
                ++profile_picker_state.selected_index;
            } else if (!(move_up || move_down)) {
                return true;
            }

            if (profile_picker_state.selected_index < app_config.launch_profiles.size()) {
                transient_status_message = "Profile: " + app_config.launch_profiles[profile_picker_state.selected_index].name;
            }
            return true;
        }

        if (watch_action_mode != eWatchActionMode::NONE && focused_pane == eFocusPane::WATCH_LIST) {
            if (event == Event::Escape) {
                watch_action_mode        = eWatchActionMode::NONE;
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                performWatchAction(watch_action_mode);

                watch_action_mode = eWatchActionMode::NONE;
                return true;
            }

            const bool handled = handleVerticalNavigation(event, watch_list_pane);
            if (handled) {
                refreshAllPanes();
            }
            return true;
        }

        if (breakpoint_action_mode != eBreakpointActionMode::NONE && focused_pane == eFocusPane::BREAKPOINTS) {
            if (event == Event::Escape) {
                breakpoint_action_mode   = eBreakpointActionMode::NONE;
                transient_status_message = {};
                return true;
            }

            if (event == Event::Return) {
                if (breakpoint_action_mode == eBreakpointActionMode::REMOVE && breakpoints_pane.selected_index < app_config.breakpoints.size()) {
                    const auto removed_source_path = app_config.breakpoints[breakpoints_pane.selected_index].source_path;
                    app_config.breakpoints.erase(app_config.breakpoints.begin() + static_cast<std::ptrdiff_t>(breakpoints_pane.selected_index));
                    if (breakpoints_pane.selected_index > 0 && breakpoints_pane.selected_index >= app_config.breakpoints.size()) {
                        --breakpoints_pane.selected_index;
                    }
                    applyBreakpointsToActiveSession();
                    if (!hasBreakpointForSource(removed_source_path)) {
                        clearBreakpointSourceInActiveSession(removed_source_path);
                    }
                    refreshAllPanes();
                }

                breakpoint_action_mode   = eBreakpointActionMode::NONE;
                transient_status_message = {};
                return true;
            }

            const bool handled = handleVerticalNavigation(event, breakpoints_pane);
            if (handled) {
                refreshAllPanes();
            }
            return true;
        }

        if (event == Event::Character('q')) {
            quitApplication();
            return true;
        }

        if (event == Event::Character('r')) {
            if (isDapControlRunning()) {
                transient_status_message = "Refresh is disabled while debugger is running";
                return true;
            }
            if (session_state == eDebuggerSessionState::TERMINATED) {
                transient_status_message = "Session terminated; reload config to start again";
                return true;
            }

            refreshAllPanes();
            return true;
        }

        if (event == Event::F5) {
            continueActiveDapSession();
            return true;
        }

        if (event == Event::F10) {
            stepOverActiveDapSession();
            return true;
        }

        if (event == Event::F11) {
            stepIntoActiveDapSession();
            return true;
        }

        if (view_visibility.show_shortcuts_overlay && (event == Event::Escape || event == Event::Character('?'))) {
            view_visibility.show_shortcuts_overlay = false;
            leader_pending                         = false;
            return true;
        }

        if (leader_pending) {
            leader_pending = false;

            if (event == Event::Escape) {
                return true;
            }

            if (event.is_character()) {
                const auto command = findCommandForKeys(keybindings, "Space " + event.character());
                if (command.has_value()) {
                    if (command.value() == eCommand::ADD_WATCH) {
                        prompt_state = beginPrompt(ePromptMode::ADD_WATCH);
                        return true;
                    }
                    if (command.value() == eCommand::EDIT_WATCH) {
                        chooseOrPerformWatchAction(eWatchActionMode::EDIT, "Choose watch with j/k, press Return to edit, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::REMOVE_WATCH) {
                        chooseOrPerformWatchAction(eWatchActionMode::REMOVE, "Choose watch with j/k, press Return to remove, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::MOVE_WATCH_UP) {
                        chooseOrPerformWatchAction(eWatchActionMode::MOVE_UP, "Choose watch with j/k, press Return to move up, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::MOVE_WATCH_DOWN) {
                        chooseOrPerformWatchAction(eWatchActionMode::MOVE_DOWN, "Choose watch with j/k, press Return to move down, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::DUPLICATE_WATCH) {
                        chooseOrPerformWatchAction(eWatchActionMode::DUPLICATE, "Choose watch with j/k, press Return to duplicate, Esc to cancel");
                        return true;
                    }
                    if (command.value() == eCommand::ADD_BREAKPOINT) {
                        prompt_state = beginPrompt(ePromptMode::ADD_BREAKPOINT);
                        return true;
                    }
                    if (command.value() == eCommand::REMOVE_BREAKPOINT) {
                        if (app_config.breakpoints.empty()) {
                            transient_status_message = "No breakpoints to remove";
                            return true;
                        }
                        if (focused_pane != eFocusPane::BREAKPOINTS) {
                            focused_pane             = eFocusPane::BREAKPOINTS;
                            breakpoint_action_mode   = eBreakpointActionMode::REMOVE;
                            transient_status_message = "Choose breakpoint with j/k, press Return to remove, Esc to cancel";
                            refreshAllPanes();
                            return true;
                        }

                        const auto removed_source_path = app_config.breakpoints[breakpoints_pane.selected_index].source_path;
                        app_config.breakpoints.erase(app_config.breakpoints.begin() + static_cast<std::ptrdiff_t>(breakpoints_pane.selected_index));
                        if (breakpoints_pane.selected_index > 0 && breakpoints_pane.selected_index >= app_config.breakpoints.size()) {
                            --breakpoints_pane.selected_index;
                        }
                        applyBreakpointsToActiveSession();
                        if (!hasBreakpointForSource(removed_source_path)) {
                            clearBreakpointSourceInActiveSession(removed_source_path);
                        }
                        refreshAllPanes();
                        return true;
                    }
                    if (command.value() == eCommand::TOGGLE_BREAKPOINT) {
                        if (app_config.breakpoints.empty()) {
                            transient_status_message = "No breakpoints to toggle";
                            return true;
                        }
                        if (focused_pane != eFocusPane::BREAKPOINTS) {
                            focused_pane             = eFocusPane::BREAKPOINTS;
                            transient_status_message = "Choose breakpoint with j/k, press Space E to toggle";
                            refreshAllPanes();
                            return true;
                        }
                        if (breakpoints_pane.selected_index < app_config.breakpoints.size()) {
                            auto& breakpoint   = app_config.breakpoints[breakpoints_pane.selected_index];
                            breakpoint.enabled = !breakpoint.enabled;
                            applyBreakpointsToActiveSession();
                            refreshAllPanes();
                        }
                        return true;
                    }
                    if (command.value() == eCommand::SET_MEMORY_TARGET) {
                        prompt_state = beginPrompt(ePromptMode::MEMORY_TARGET, manual_memory_target);
                        return true;
                    }
                    if (command.value() == eCommand::CYCLE_THEME) {
                        theme_picker_state = {
                            .active          = true,
                            .original_preset = active_theme_preset,
                            .selected_index  = themePresetIndex(active_theme_preset),
                        };
                        transient_status_message = "Theme preview: " + themePresetName(active_theme_preset);
                        return true;
                    }
                    if (command.value() == eCommand::CHOOSE_PROFILE) {
                        if (app_config.launch_profiles.empty()) {
                            transient_status_message = "No launch profiles configured";
                            return true;
                        }
                        if (isDapControlRunning() || session_state == eDebuggerSessionState::RUNNING || session_state == eDebuggerSessionState::TERMINATING) {
                            transient_status_message = "Profile picker is unavailable while debugger is running";
                            return true;
                        }
                        profile_picker_state = {
                            .active         = true,
                            .selected_index = launchProfileIndex(app_config.launch_profiles, app_config.active_profile),
                        };
                        transient_status_message = "Profile: " + app_config.launch_profiles[profile_picker_state.selected_index].name;
                        return true;
                    }
                    if (command.value() == eCommand::RELOAD_CONFIG) {
                        reloadConfig();
                        return true;
                    }
                    if (command.value() == eCommand::CONTINUE_EXECUTION) {
                        continueActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::STEP_OVER) {
                        stepOverActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::STEP_INTO) {
                        stepIntoActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::STEP_OUT) {
                        stepOutActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::PAUSE_EXECUTION) {
                        pauseActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::TERMINATE_SESSION) {
                        terminateActiveDapSession();
                        return true;
                    }
                    if (command.value() == eCommand::RESTART_SESSION) {
                        restartSession();
                        return true;
                    }
                    executeCommand(command.value(), focused_pane, view_visibility);
                    refreshAllPanes();
                    return true;
                }
            }

            return true;
        }

        if (event == Event::Character(' ')) {
            leader_pending = true;
            return true;
        }

        if (event == Event::Tab) {
            focused_pane = advanceFocusPane(focused_pane, view_visibility);
            refreshAllPanes();
            return true;
        }

        if (focused_pane == eFocusPane::THREADS) {
            const bool handled = handleVerticalNavigation(event, threads_pane);
            if (handled) {
                if (threads_pane.selected_index < stopped_context.threads.size()) {
                    debug_selection.thread_id   = stopped_context.threads[threads_pane.selected_index].id;
                    debug_selection.frame_index = 0;
                    memory_navigation_offset    = 0;

                    if (auto* dap_session = dynamic_cast<CDapDebugSession*>(debug_session.get()); dap_session != nullptr && session_state == eDebuggerSessionState::STOPPED) {
                        stopped_context = updateDapStoppedContext(*dap_session, debug_selection, disassembly_start_address, disassembly_memory_reference);
                    }

                    refreshAllPanes();
                }
            }
            return handled;
        }

        if (focused_pane == eFocusPane::STACK) {
            const bool handled = handleVerticalNavigation(event, stack_pane);
            if (handled) {
                if (stack_pane.selected_index < stopped_context.stack_frames.size()) {
                    debug_selection.frame_index = stack_pane.selected_index;
                    stopped_context.location    = stackFrameLocation(stopped_context.stack_frames[stack_pane.selected_index]);
                    memory_navigation_offset    = 0;
                    refreshAllPanes();
                }
            }
            return handled;
        }

        if (focused_pane == eFocusPane::LOCALS) {
            const bool handled = handleVerticalNavigation(event, locals_pane);
            if (handled) {
                memory_navigation_offset = 0;
                refreshAllPanes();
            }
            return handled;
        }
        if (focused_pane == eFocusPane::WATCH_LIST) {
            const bool handled = handleVerticalNavigation(event, watch_list_pane);
            if (handled) {
                memory_navigation_offset = 0;
                refreshAllPanes();
            }
            return handled;
        }
        if (focused_pane == eFocusPane::BREAKPOINTS) {
            const bool handled = handleVerticalNavigation(event, breakpoints_pane);
            if (handled) {
                refreshAllPanes();
            }
            return handled;
        }
        if (focused_pane == eFocusPane::MEMORY_VIEW) {
            if (const auto navigation_delta = memoryNavigationDelta(event, 8, memory_view_pane.rows.size()); navigation_delta.has_value()) {
                memory_navigation_offset += navigation_delta.value();
                refreshAllPanes();
                return true;
            }
            return handleVerticalNavigation(event, memory_view_pane);
        }
        if (focused_pane == eFocusPane::DISASSEMBLY_VIEW) {
            return handleVerticalNavigation(event, disassembly_pane);
        }

        return false;
    });

    screen.Loop(component);
}
