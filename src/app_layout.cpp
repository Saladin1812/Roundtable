#include "app_layout.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <ftxui/dom/elements.hpp>

#include "session_status.hpp"

namespace {

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
                hex_element   = hex_element | bgcolor(row_background);
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
                hint  = "Enter source.cpp:line, for example src/main.cpp:42";
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

SPromptState beginPrompt(ePromptMode mode, std::string initial_input, bool replace_on_input) {
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

ftxui::Element renderRoundtableLayout(const SAppLayoutState& state) {
    using namespace ftxui;

    Element  locals          = renderSelectablePane(state.locals_pane, state.focused_pane == eFocusPane::LOCALS, state.theme);
    Element  threads         = renderSelectablePane(state.threads_pane, state.focused_pane == eFocusPane::THREADS, state.theme);
    Element  stack           = renderSelectablePane(state.stack_pane, state.focused_pane == eFocusPane::STACK, state.theme);
    Element  watch_list      = renderSelectablePane(state.watch_list_pane, state.focused_pane == eFocusPane::WATCH_LIST, state.theme);
    Element  breakpoints     = renderSelectablePane(state.breakpoints_pane, state.focused_pane == eFocusPane::BREAKPOINTS, state.theme);
    Element  auxiliary_views = renderAuxiliaryViews(state.view_visibility, state.memory_view_pane, state.disassembly_pane, state.focused_pane, state.theme, state.memory_context);
    Element  left_column     = vbox({
        locals | flex,
        threads | size(HEIGHT, EQUAL, 6),
        stack | size(HEIGHT, EQUAL, 8),
    });
    Element  right_column    = vbox({
        watch_list | flex,
        breakpoints | size(HEIGHT, EQUAL, 8),
    });

    Elements runtime_status_items = {
        text(" Roundtable ") | bgcolor(state.theme.selected_background) | color(state.theme.selected_foreground),
        separator(),
        text(" " + state.current_status + " ") | color(state.theme.chrome),
    };

    Elements frame_status_items = {
        text(" " + formatStoppedContextStatus(state.stopped_context, state.debug_selection) + " ") | color(state.theme.chrome),
    };

    Elements command_status_items = {
        text(" Tab cycle ") | color(state.theme.chrome),      separator(), text(" r refresh ") | color(state.theme.chrome), separator(),
        text(" Space commands ") | color(state.theme.accent), separator(), text(" q quit ") | color(state.theme.chrome),
    };

    Element status_bar = vbox({
                             hbox(runtime_status_items),
                             separator(),
                             hbox(frame_status_items),
                             separator(),
                             hbox(command_status_items),
                         }) |
        border | color(state.theme.chrome);

    Element content = vbox({
        hbox({
            left_column | size(WIDTH, EQUAL, 28),
            auxiliary_views | flex,
            right_column | size(WIDTH, EQUAL, 28),
        }) | flex,
        status_bar,
    });

    if (state.view_visibility.show_shortcuts_overlay) {
        content = dbox({
            content,
            renderShortcutsOverlay(state.keybindings, state.theme) | center,
        });
    }

    if (state.leader_pending) {
        content = dbox({
            content,
            vbox({
                filler(),
                renderLeaderPopup(state.keybindings, state.focused_pane, state.theme) | center,
                filler() | size(HEIGHT, EQUAL, 5),
            }),
        });
    }

    if (state.theme_picker_state.active) {
        content = dbox({
            content,
            renderThemePickerOverlay(state.theme_picker_state, state.theme) | center,
        });
    }

    if (state.profile_picker_state.active) {
        content = dbox({
            content,
            renderProfilePickerOverlay(state.profile_picker_state, state.launch_profiles, state.theme) | center,
        });
    }

    if (state.prompt_state.mode != ePromptMode::NONE) {
        content = dbox({
            content,
            renderPromptOverlay(state.prompt_state, state.theme) | center,
        });
    }

    return content;
}
